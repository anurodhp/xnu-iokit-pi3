// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */

#include "vchiq_xnu.h"

#include "vchiq_core.h"
#include "vchiq_ioctl.h"
#include "vchiq_arm.h"
#include "vchiq_debugfs.h"
#include "vchiq_xnu_platform.h"

/*
 * XNU port: the file operations are entry points for BCM2837VCHIQUserClient
 * (vchiq_xnu_platform.h). ioctl arguments arrive already copied into the
 * kernel, and go back to the client after the call, so the reads and writes
 * of the argument struct below are plain memory accesses (marked "XNU:");
 * pointers inside it are still the client's and still go through
 * copy_from_user()/copy_to_user(). There is no 32-bit compat ABI.
 */

static const char *const ioctl_names[] = {
	"CONNECT",
	"SHUTDOWN",
	"CREATE_SERVICE",
	"REMOVE_SERVICE",
	"QUEUE_MESSAGE",
	"QUEUE_BULK_TRANSMIT",
	"QUEUE_BULK_RECEIVE",
	"AWAIT_COMPLETION",
	"DEQUEUE_MESSAGE",
	"GET_CLIENT_ID",
	"GET_CONFIG",
	"CLOSE_SERVICE",
	"USE_SERVICE",
	"RELEASE_SERVICE",
	"SET_SERVICE_OPTION",
	"DUMP_PHYS_MEM",
	"LIB_VERSION",
	"CLOSE_DELIVERED"
};

static_assert(ARRAY_SIZE(ioctl_names) == (VCHIQ_IOC_MAX + 1));

static void
user_service_free(void *userdata)
{
	kfree(userdata);
}

static void close_delivered(struct user_service *user_service)
{
	vchiq_log_info(vchiq_arm_log_level,
		       "%s(handle=%x)",
		       __func__, user_service->service->handle);

	if (user_service->close_pending) {
		/* Allow the underlying service to be culled */
		vchiq_service_put(user_service->service);

		/* Wake the user-thread blocked in close_ or remove_service */
		complete(&user_service->close_event);

		user_service->close_pending = 0;
	}
}

struct vchiq_io_copy_callback_context {
	struct vchiq_element *element;
	size_t element_offset;
	unsigned long elements_to_go;
};

static ssize_t vchiq_ioc_copy_element_data(void *context, void *dest,
					   size_t offset, size_t maxsize)
{
	struct vchiq_io_copy_callback_context *cc = context;
	size_t total_bytes_copied = 0;
	size_t bytes_this_round;

	while (total_bytes_copied < maxsize) {
		if (!cc->elements_to_go)
			return total_bytes_copied;

		if (!cc->element->size) {
			cc->elements_to_go--;
			cc->element++;
			cc->element_offset = 0;
			continue;
		}

		bytes_this_round = min(cc->element->size - cc->element_offset,
				       maxsize - total_bytes_copied);

		if (copy_from_user(dest + total_bytes_copied,
				   cc->element->data + cc->element_offset,
				   bytes_this_round))
			return -EFAULT;

		cc->element_offset += bytes_this_round;
		total_bytes_copied += bytes_this_round;

		if (cc->element_offset == cc->element->size) {
			cc->elements_to_go--;
			cc->element++;
			cc->element_offset = 0;
		}
	}

	return maxsize;
}

static int
vchiq_ioc_queue_message(struct vchiq_instance *instance, unsigned int handle,
			struct vchiq_element *elements, unsigned long count)
{
	struct vchiq_io_copy_callback_context context;
	int status = 0;
	unsigned long i;
	size_t total_size = 0;

	context.element = elements;
	context.element_offset = 0;
	context.elements_to_go = count;

	for (i = 0; i < count; i++) {
		if (!elements[i].data && elements[i].size != 0)
			return -EFAULT;

		total_size += elements[i].size;
	}

	status = vchiq_queue_message(instance, handle, vchiq_ioc_copy_element_data,
				     &context, total_size);

	if (status == -EINVAL)
		return -EIO;
	else if (status == -EAGAIN)
		return -EINTR;
	return 0;
}

static int vchiq_ioc_create_service(struct vchiq_instance *instance,
				    struct vchiq_create_service *args)
{
	struct user_service *user_service = NULL;
	struct vchiq_service *service;
	int status = 0;
	struct vchiq_service_params_kernel params;
	int srvstate;

	if (args->is_open && !instance->connected)
		return -ENOTCONN;

	user_service = kmalloc(sizeof(*user_service), GFP_KERNEL);
	if (!user_service)
		return -ENOMEM;

	if (args->is_open) {
		srvstate = VCHIQ_SRVSTATE_OPENING;
	} else {
		srvstate = instance->connected ?
			 VCHIQ_SRVSTATE_LISTENING : VCHIQ_SRVSTATE_HIDDEN;
	}

	params = (struct vchiq_service_params_kernel) {
		.fourcc   = args->params.fourcc,
		.callback = service_callback,
		.userdata = user_service,
		.version  = args->params.version,
		.version_min = args->params.version_min,
	};
	service = vchiq_add_service_internal(instance->state, &params,
					     srvstate, instance,
					     user_service_free);
	if (!service) {
		kfree(user_service);
		return -EEXIST;
	}

	user_service->service = service;
	user_service->userdata = args->params.userdata;
	user_service->instance = instance;
	user_service->is_vchi = (args->is_vchi != 0);
	user_service->dequeue_pending = 0;
	user_service->close_pending = 0;
	user_service->message_available_pos = instance->completion_remove - 1;
	user_service->msg_insert = 0;
	user_service->msg_remove = 0;
	init_completion(&user_service->insert_event);
	init_completion(&user_service->remove_event);
	init_completion(&user_service->close_event);

	if (args->is_open) {
		status = vchiq_open_service_internal(service, instance->pid);
		if (status) {
			vchiq_remove_service(instance, service->handle);
			return (status == -EAGAIN) ?
				-EINTR : -EIO;
		}
	}
	args->handle = service->handle;

	return 0;
}

static int vchiq_ioc_dequeue_message(struct vchiq_instance *instance,
				     struct vchiq_dequeue_message *args)
{
	struct user_service *user_service;
	struct vchiq_service *service;
	struct vchiq_header *header;
	int ret;

	DEBUG_INITIALISE(g_state.local);
	DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
	service = find_service_for_instance(instance, args->handle);
	if (!service)
		return -EINVAL;

	user_service = (struct user_service *)service->base.userdata;
	if (user_service->is_vchi == 0) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&msg_queue_spinlock);
	if (user_service->msg_remove == user_service->msg_insert) {
		if (!args->blocking) {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
			ret = -EWOULDBLOCK;
			goto out;
		}
		user_service->dequeue_pending = 1;
		ret = 0;
		do {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
			if (wait_for_completion_interruptible(&user_service->insert_event)) {
				vchiq_log_info(vchiq_arm_log_level,
					       "DEQUEUE_MESSAGE interrupted");
				ret = -EINTR;
				break;
			}
			spin_lock(&msg_queue_spinlock);
		} while (user_service->msg_remove == user_service->msg_insert);

		if (ret)
			goto out;
	}

	if (WARN_ON_ONCE((int)(user_service->msg_insert -
			 user_service->msg_remove) < 0)) {
		spin_unlock(&msg_queue_spinlock);
		ret = -EINVAL;
		goto out;
	}

	header = user_service->msg_queue[user_service->msg_remove &
		(MSG_QUEUE_SIZE - 1)];
	user_service->msg_remove++;
	spin_unlock(&msg_queue_spinlock);

	complete(&user_service->remove_event);
	if (!header) {
		ret = -ENOTCONN;
	} else if (header->size <= args->bufsize) {
		/* Copy to user space if msgbuf is not NULL */
		if (!args->buf || (copy_to_user(args->buf, header->data, header->size) == 0)) {
			ret = header->size;
			vchiq_release_message(instance, service->handle, header);
		} else {
			ret = -EFAULT;
		}
	} else {
		vchiq_log_error(vchiq_arm_log_level,
				"header %pK: bufsize %x < size %x",
				header, args->bufsize, header->size);
		WARN(1, "invalid size\n");
		ret = -EMSGSIZE;
	}
	DEBUG_TRACE(DEQUEUE_MESSAGE_LINE);
out:
	vchiq_service_put(service);
	return ret;
}

static int vchiq_irq_queue_bulk_tx_rx(struct vchiq_instance *instance,
				      struct vchiq_queue_bulk_transfer *args,
				      enum vchiq_bulk_dir dir,
				      enum vchiq_bulk_mode *mode)
{
	struct vchiq_service *service;
	struct bulk_waiter_node *waiter = NULL, *iter;
	void *userdata;
	int status = 0;
	int ret;

	service = find_service_for_instance(instance, args->handle);
	if (!service)
		return -EINVAL;

	if (args->mode == VCHIQ_BULK_MODE_BLOCKING) {
		waiter = kzalloc(sizeof(*waiter), GFP_KERNEL);
		if (!waiter) {
			ret = -ENOMEM;
			goto out;
		}

		userdata = &waiter->bulk_waiter;
	} else if (args->mode == VCHIQ_BULK_MODE_WAITING) {
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_for_each_entry(iter, &instance->bulk_waiter_list,
				    list) {
			if (iter->pid == vchiq_xnu_current_tid()) {
				list_del(&iter->list);
				waiter = iter;
				break;
			}
		}
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		if (!waiter) {
			vchiq_log_error(vchiq_arm_log_level,
					"no bulk_waiter found for pid %d", vchiq_xnu_current_tid());
			ret = -ESRCH;
			goto out;
		}
		vchiq_log_info(vchiq_arm_log_level,
			       "found bulk_waiter %pK for pid %d", waiter, vchiq_xnu_current_tid());
		userdata = &waiter->bulk_waiter;
	} else {
		userdata = args->userdata;
	}

	status = vchiq_bulk_transfer(instance, args->handle, NULL, args->data, args->size,
				     userdata, args->mode, dir);

	if (!waiter) {
		ret = 0;
		goto out;
	}

	if ((status != -EAGAIN) || fatal_signal_pending(current) ||
	    !waiter->bulk_waiter.bulk) {
		if (waiter->bulk_waiter.bulk) {
			/* Cancel the signal when the transfer completes. */
			spin_lock(&bulk_waiter_spinlock);
			waiter->bulk_waiter.bulk->userdata = NULL;
			spin_unlock(&bulk_waiter_spinlock);
		}
		kfree(waiter);
		ret = 0;
	} else {
		const enum vchiq_bulk_mode mode_waiting =
			VCHIQ_BULK_MODE_WAITING;
		waiter->pid = vchiq_xnu_current_tid();
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_add(&waiter->list, &instance->bulk_waiter_list);
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		vchiq_log_info(vchiq_arm_log_level,
			       "saved bulk_waiter %pK for pid %d", waiter, vchiq_xnu_current_tid());

		*mode = mode_waiting;	/* XNU: the argument struct is a kernel copy */
		ret = 0;
	}
out:
	vchiq_service_put(service);
	if (ret)
		return ret;
	else if (status == -EINVAL)
		return -EIO;
	else if (status == -EAGAIN)
		return -EINTR;
	return 0;
}

/* read a user pointer value from an array pointers in user space */
static inline int vchiq_get_user_ptr(void __user **buf, void __user *ubuf, int index)
{
	int ret;

	uintptr_t ptr, __user *uptr = ubuf;

	ret = get_user(ptr, uptr + index);

	if (ret)
		return ret;

	*buf = (void __user *)ptr;

	return 0;
}

static int vchiq_put_completion(struct vchiq_completion_data __user *buf,
				struct vchiq_completion_data *completion,
				int index)
{
	if (copy_to_user(&buf[index], completion, sizeof(*completion)))
		return -EFAULT;

	return 0;
}

static int vchiq_ioc_await_completion(struct vchiq_instance *instance,
				      struct vchiq_await_completion *args,
				      unsigned int *msgbufcountp)
{
	int msgbufcount;
	int remove;
	int ret;

	DEBUG_INITIALISE(g_state.local);

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	if (!instance->connected)
		return -ENOTCONN;

	mutex_lock(&instance->completion_mutex);

	DEBUG_TRACE(AWAIT_COMPLETION_LINE);
	while ((instance->completion_remove == instance->completion_insert) && !instance->closing) {
		int rc;

		DEBUG_TRACE(AWAIT_COMPLETION_LINE);
		mutex_unlock(&instance->completion_mutex);
		rc = wait_for_completion_interruptible(&instance->insert_event);
		mutex_lock(&instance->completion_mutex);
		if (rc) {
			DEBUG_TRACE(AWAIT_COMPLETION_LINE);
			vchiq_log_info(vchiq_arm_log_level,
				       "AWAIT_COMPLETION interrupted");
			ret = -EINTR;
			goto out;
		}
	}
	DEBUG_TRACE(AWAIT_COMPLETION_LINE);

	msgbufcount = args->msgbufcount;
	remove = instance->completion_remove;

	for (ret = 0; ret < args->count; ret++) {
		struct vchiq_completion_data_kernel *completion;
		struct vchiq_completion_data user_completion;
		struct vchiq_service *service;
		struct user_service *user_service;
		struct vchiq_header *header;

		if (remove == instance->completion_insert)
			break;

		completion = &instance->completions[remove & (MAX_COMPLETIONS - 1)];

		/*
		 * A read memory barrier is needed to stop
		 * prefetch of a stale completion record
		 */
		rmb();

		service = completion->service_userdata;
		user_service = service->base.userdata;

		memset(&user_completion, 0, sizeof(user_completion));
		user_completion = (struct vchiq_completion_data) {
			.reason = completion->reason,
			.service_userdata = user_service->userdata,
		};

		header = completion->header;
		if (header) {
			void __user *msgbuf;
			int msglen;

			msglen = header->size + sizeof(struct vchiq_header);
			/* This must be a VCHIQ-style service */
			if (args->msgbufsize < msglen) {
				vchiq_log_error(vchiq_arm_log_level,
						"header %pK: msgbufsize %x < msglen %x",
						header, args->msgbufsize, msglen);
						WARN(1, "invalid message size\n");
				if (ret == 0)
					ret = -EMSGSIZE;
				break;
			}
			if (msgbufcount <= 0)
				/* Stall here for lack of a buffer for the message. */
				break;
			/* Get the pointer from user space */
			msgbufcount--;
			if (vchiq_get_user_ptr(&msgbuf, args->msgbufs,
					       msgbufcount)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}

			/* Copy the message to user space */
			if (copy_to_user(msgbuf, header, msglen)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}

			/* Now it has been copied, the message can be released. */
			vchiq_release_message(instance, service->handle, header);

			/* The completion must point to the msgbuf. */
			user_completion.header = msgbuf;
		}

		if ((completion->reason == VCHIQ_SERVICE_CLOSED) &&
		    !instance->use_close_delivered)
			vchiq_service_put(service);

		/*
		 * FIXME: address space mismatch, does bulk_userdata
		 * actually point to user or kernel memory?
		 */
		user_completion.bulk_userdata = completion->bulk_userdata;

		if (vchiq_put_completion(args->buf, &user_completion, ret)) {
			if (ret == 0)
				ret = -EFAULT;
			break;
		}

		/*
		 * Ensure that the above copy has completed
		 * before advancing the remove pointer.
		 */
		mb();
		remove++;
		instance->completion_remove = remove;
	}

	if (msgbufcount != args->msgbufcount) {
		*msgbufcountp = msgbufcount;	/* XNU: the argument struct is a kernel copy */
	}
out:
	if (ret)
		complete(&instance->remove_event);
	mutex_unlock(&instance->completion_mutex);
	DEBUG_TRACE(AWAIT_COMPLETION_LINE);

	return ret;
}

uint32_t
vchiq_xnu_ioctl_size(uint32_t nr)
{
	switch (nr) {
	case _IOC_NR(VCHIQ_IOC_CREATE_SERVICE):		return sizeof(struct vchiq_create_service);
	case _IOC_NR(VCHIQ_IOC_QUEUE_MESSAGE):		return sizeof(struct vchiq_queue_message);
	case _IOC_NR(VCHIQ_IOC_QUEUE_BULK_TRANSMIT):	return sizeof(struct vchiq_queue_bulk_transfer);
	case _IOC_NR(VCHIQ_IOC_QUEUE_BULK_RECEIVE):	return sizeof(struct vchiq_queue_bulk_transfer);
	case _IOC_NR(VCHIQ_IOC_AWAIT_COMPLETION):	return sizeof(struct vchiq_await_completion);
	case _IOC_NR(VCHIQ_IOC_DEQUEUE_MESSAGE):	return sizeof(struct vchiq_dequeue_message);
	case _IOC_NR(VCHIQ_IOC_GET_CONFIG):		return sizeof(struct vchiq_get_config);
	case _IOC_NR(VCHIQ_IOC_SET_SERVICE_OPTION):	return sizeof(struct vchiq_set_service_option);
	case _IOC_NR(VCHIQ_IOC_DUMP_PHYS_MEM):		return sizeof(struct vchiq_dump_mem);
	default:					return 0;
	}
}

long
vchiq_xnu_ioctl(struct vchiq_instance *instance, unsigned int cmd, unsigned long arg,
		void *kargs)
{
	int status = 0;
	struct vchiq_service *service = NULL;
	long ret = 0;
	int i, rc;

	vchiq_log_trace(vchiq_arm_log_level,
			"%s - instance %pK, cmd %s, arg %lx", __func__, instance,
			((_IOC_TYPE(cmd) == VCHIQ_IOC_MAGIC) && (_IOC_NR(cmd) <= VCHIQ_IOC_MAX)) ?
			ioctl_names[_IOC_NR(cmd)] : "<invalid>", arg);

	switch (cmd) {
	case VCHIQ_IOC_SHUTDOWN:
		if (!instance->connected)
			break;

		/* Remove all services */
		i = 0;
		while ((service = next_service_by_instance(instance->state,
							   instance, &i))) {
			status = vchiq_remove_service(instance, service->handle);
			vchiq_service_put(service);
			if (status)
				break;
		}
		service = NULL;

		if (!status) {
			/* Wake the completion thread and ask it to exit */
			instance->closing = 1;
			complete(&instance->insert_event);
		}

		break;

	case VCHIQ_IOC_CONNECT:
		if (instance->connected) {
			ret = -EINVAL;
			break;
		}
		rc = mutex_lock_killable(&instance->state->mutex);
		if (rc) {
			vchiq_log_error(vchiq_arm_log_level,
					"vchiq: connect: could not lock mutex for state %d: %d",
					instance->state->id, rc);
			ret = -EINTR;
			break;
		}
		status = vchiq_connect_internal(instance->state, instance);
		mutex_unlock(&instance->state->mutex);

		if (!status)
			instance->connected = 1;
		else
			vchiq_log_error(vchiq_arm_log_level,
					"vchiq: could not connect: %d", status);
		break;

	case VCHIQ_IOC_CREATE_SERVICE: {
		struct vchiq_create_service *argp = kargs;	/* XNU */
		struct vchiq_create_service args;

		memcpy(&args, argp, sizeof(args));

		ret = vchiq_ioc_create_service(instance, &args);
		if (ret < 0)
			break;

		argp->handle = args.handle;
	} break;

	case VCHIQ_IOC_CLOSE_SERVICE:
	case VCHIQ_IOC_REMOVE_SERVICE: {
		unsigned int handle = (unsigned int)arg;
		struct user_service *user_service;

		service = find_service_for_instance(instance, handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		user_service = service->base.userdata;

		/*
		 * close_pending is false on first entry, and when the
		 * wait in vchiq_close_service has been interrupted.
		 */
		if (!user_service->close_pending) {
			status = (cmd == VCHIQ_IOC_CLOSE_SERVICE) ?
				 vchiq_close_service(instance, service->handle) :
				 vchiq_remove_service(instance, service->handle);
			if (status)
				break;
		}

		/*
		 * close_pending is true once the underlying service
		 * has been closed until the client library calls the
		 * CLOSE_DELIVERED ioctl, signalling close_event.
		 */
		if (user_service->close_pending &&
		    wait_for_completion_interruptible(&user_service->close_event))
			status = -EAGAIN;
		break;
	}

	case VCHIQ_IOC_USE_SERVICE:
	case VCHIQ_IOC_RELEASE_SERVICE:	{
		unsigned int handle = (unsigned int)arg;

		service = find_service_for_instance(instance, handle);
		if (service) {
			ret = (cmd == VCHIQ_IOC_USE_SERVICE) ?
				vchiq_use_service_internal(service) :
				vchiq_release_service_internal(service);
			if (ret) {
				vchiq_log_error(vchiq_susp_log_level,
						"%s: cmd %s returned error %ld for service %c%c%c%c:%03d",
						__func__, (cmd == VCHIQ_IOC_USE_SERVICE) ?
						"VCHIQ_IOC_USE_SERVICE" :
						"VCHIQ_IOC_RELEASE_SERVICE",
					ret,
					VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
					service->client_id);
			}
		} else {
			ret = -EINVAL;
		}
	} break;

	case VCHIQ_IOC_QUEUE_MESSAGE: {
		struct vchiq_queue_message args;

		memcpy(&args, kargs, sizeof(args));	/* XNU */

		service = find_service_for_instance(instance, args.handle);

		if (service && (args.count <= MAX_ELEMENTS)) {
			/* Copy elements into kernel space */
			struct vchiq_element elements[MAX_ELEMENTS];

			if (copy_from_user(elements, args.elements,
					   args.count * sizeof(struct vchiq_element)) == 0)
				ret = vchiq_ioc_queue_message(instance, args.handle, elements,
							      args.count);
			else
				ret = -EFAULT;
		} else {
			ret = -EINVAL;
		}
	} break;

	case VCHIQ_IOC_QUEUE_BULK_TRANSMIT:
	case VCHIQ_IOC_QUEUE_BULK_RECEIVE: {
		struct vchiq_queue_bulk_transfer args;
		struct vchiq_queue_bulk_transfer *argp = kargs;	/* XNU */

		enum vchiq_bulk_dir dir =
			(cmd == VCHIQ_IOC_QUEUE_BULK_TRANSMIT) ?
			VCHIQ_BULK_TRANSMIT : VCHIQ_BULK_RECEIVE;

		memcpy(&args, argp, sizeof(args));

		ret = vchiq_irq_queue_bulk_tx_rx(instance, &args,
						 dir, &argp->mode);
	} break;

	case VCHIQ_IOC_AWAIT_COMPLETION: {
		struct vchiq_await_completion args;
		struct vchiq_await_completion *argp = kargs;	/* XNU */

		memcpy(&args, argp, sizeof(args));

		ret = vchiq_ioc_await_completion(instance, &args,
						 &argp->msgbufcount);
	} break;

	case VCHIQ_IOC_DEQUEUE_MESSAGE: {
		struct vchiq_dequeue_message args;

		memcpy(&args, kargs, sizeof(args));	/* XNU */

		ret = vchiq_ioc_dequeue_message(instance, &args);
	} break;

	case VCHIQ_IOC_GET_CLIENT_ID: {
		unsigned int handle = (unsigned int)arg;

		ret = vchiq_get_client_id(instance, handle);
	} break;

	case VCHIQ_IOC_GET_CONFIG: {
		struct vchiq_get_config args;
		struct vchiq_config config;

		memcpy(&args, kargs, sizeof(args));	/* XNU */
		if (args.config_size > sizeof(config)) {
			ret = -EINVAL;
			break;
		}

		vchiq_get_config(&config);
		if (copy_to_user(args.pconfig, &config, args.config_size)) {
			ret = -EFAULT;
			break;
		}
	} break;

	case VCHIQ_IOC_SET_SERVICE_OPTION: {
		struct vchiq_set_service_option args;

		memcpy(&args, kargs, sizeof(args));	/* XNU */

		service = find_service_for_instance(instance, args.handle);
		if (!service) {
			ret = -EINVAL;
			break;
		}

		ret = vchiq_set_service_option(instance, args.handle, args.option,
					       args.value);
	} break;

	case VCHIQ_IOC_LIB_VERSION: {
		unsigned int lib_version = (unsigned int)arg;

		if (lib_version < VCHIQ_VERSION_MIN)
			ret = -EINVAL;
		else if (lib_version >= VCHIQ_VERSION_CLOSE_DELIVERED)
			instance->use_close_delivered = 1;
	} break;

	case VCHIQ_IOC_CLOSE_DELIVERED: {
		unsigned int handle = (unsigned int)arg;

		service = find_closed_service_for_instance(instance, handle);
		if (service) {
			struct user_service *user_service =
				(struct user_service *)service->base.userdata;
			close_delivered(user_service);
		} else {
			ret = -EINVAL;
		}
	} break;

	default:
		ret = -ENOTTY;
		break;
	}

	if (service)
		vchiq_service_put(service);

	if (ret == 0) {
		if (status == -EINVAL)
			ret = -EIO;
		else if (status == -EAGAIN)
			ret = -EINTR;
	}

	if (!status && (ret < 0) && (ret != -EINTR) && (ret != -EWOULDBLOCK))
		vchiq_log_info(vchiq_arm_log_level,
			       "  ioctl instance %pK, cmd %s -> status %d, %ld",
			       instance, (_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
			       ioctl_names[_IOC_NR(cmd)] : "<invalid>", status, ret);
	else
		vchiq_log_trace(vchiq_arm_log_level,
				"  ioctl instance %pK, cmd %s -> status %d, %ld",
				instance, (_IOC_NR(cmd) <= VCHIQ_IOC_MAX) ?
				ioctl_names[_IOC_NR(cmd)] : "<invalid>", status, ret);

	return ret;
}

struct vchiq_instance *
vchiq_xnu_open(int *err)
{
	struct vchiq_state *state = vchiq_get_state();
	struct vchiq_instance *instance;

	vchiq_log_info(vchiq_arm_log_level, "vchiq_open");

	if (!state) {
		vchiq_log_error(vchiq_arm_log_level,
				"vchiq has no connection to VideoCore");
		*err = -ENOTCONN;
		return NULL;
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance) {
		*err = -ENOMEM;
		return NULL;
	}

	instance->state = state;
	instance->pid = vchiq_xnu_current_pid();

	vchiq_debugfs_add_instance(instance);

	init_completion(&instance->insert_event);
	init_completion(&instance->remove_event);
	mutex_init(&instance->completion_mutex);
	mutex_init(&instance->bulk_waiter_list_mutex);
	INIT_LIST_HEAD(&instance->bulk_waiter_list);

	*err = 0;
	return instance;
}

void
vchiq_xnu_release(struct vchiq_instance *instance)
{
	struct vchiq_state *state = vchiq_get_state();
	struct vchiq_service *service;
	int ret = 0;
	int i;

	vchiq_log_info(vchiq_arm_log_level, "%s: instance=%lx", __func__,
		       (unsigned long)instance);

	if (!state) {
		ret = -EPERM;
		goto out;
	}

	/* Ensure videocore is awake to allow termination. */
	vchiq_use_internal(instance->state, NULL, USE_TYPE_VCHIQ);

	mutex_lock(&instance->completion_mutex);

	/* Wake the completion thread and ask it to exit */
	instance->closing = 1;
	complete(&instance->insert_event);

	mutex_unlock(&instance->completion_mutex);

	/* Wake the slot handler if the completion queue is full. */
	complete(&instance->remove_event);

	/* Mark all services for termination... */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i))) {
		struct user_service *user_service = service->base.userdata;

		/* Wake the slot handler if the msg queue is full. */
		complete(&user_service->remove_event);

		vchiq_terminate_service_internal(service);
		vchiq_service_put(service);
	}

	/* ...and wait for them to die */
	i = 0;
	while ((service = next_service_by_instance(state, instance, &i))) {
		struct user_service *user_service = service->base.userdata;

		wait_for_completion(&service->remove_event);

		if (WARN_ON(service->srvstate != VCHIQ_SRVSTATE_FREE)) {
			vchiq_service_put(service);
			break;
		}

		spin_lock(&msg_queue_spinlock);

		while (user_service->msg_remove != user_service->msg_insert) {
			struct vchiq_header *header;
			int m = user_service->msg_remove & (MSG_QUEUE_SIZE - 1);

			header = user_service->msg_queue[m];
			user_service->msg_remove++;
			spin_unlock(&msg_queue_spinlock);

			if (header)
				vchiq_release_message(instance, service->handle, header);
			spin_lock(&msg_queue_spinlock);
		}

		spin_unlock(&msg_queue_spinlock);

		vchiq_service_put(service);
	}

	/* Release any closed services */
	while (instance->completion_remove != instance->completion_insert) {
		struct vchiq_completion_data_kernel *completion;
		struct vchiq_service *service;

		completion = &instance->completions[instance->completion_remove
						    & (MAX_COMPLETIONS - 1)];
		service = completion->service_userdata;
		if (completion->reason == VCHIQ_SERVICE_CLOSED) {
			struct user_service *user_service =
							service->base.userdata;

			/* Wake any blocked user-thread */
			if (instance->use_close_delivered)
				complete(&user_service->close_event);
			vchiq_service_put(service);
		}
		instance->completion_remove++;
	}

	/* Release the PEER service count. */
	vchiq_release_internal(instance->state, NULL);

	free_bulk_waiter(instance);

	vchiq_debugfs_remove_instance(instance);

	kfree(instance);

out:
	(void)ret;
}

