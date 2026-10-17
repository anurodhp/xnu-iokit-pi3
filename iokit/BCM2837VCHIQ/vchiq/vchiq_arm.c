// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2014 Raspberry Pi (Trading) Ltd. All rights reserved.
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 */


/*
 * XNU port: the platform half of Linux's vchiq_arm.c (platform_device probe,
 * DT parsing, dma-mapping of the slots and of user pages, the doorbell IRQ,
 * the character device registration) is BCM2837VCHIQ.cpp, reached through
 * vchiq_xnu_platform.h. What stays here is the instance / service / keepalive
 * logic, unchanged apart from the marked lines.
 */
#include "vchiq_xnu.h"
#include "vchiq_core.h"
#include "vchiq_ioctl.h"
#include "vchiq_arm.h"
#include "vchiq_debugfs.h"
#include "vchiq_xnu_platform.h"

#define VCHIQ_PLATFORM_FRAGMENTS_OFFSET_IDX 0
#define VCHIQ_PLATFORM_FRAGMENTS_COUNT_IDX  1

#define KEEPALIVE_VER 1
#define KEEPALIVE_VER_MIN KEEPALIVE_VER

/* Run time control of log level, based on KERN_XXX level. */
int vchiq_arm_log_level = VCHIQ_LOG_DEFAULT;
int vchiq_susp_log_level = VCHIQ_LOG_ERROR;

DEFINE_SPINLOCK(msg_queue_spinlock);
struct vchiq_state g_state;

struct vchiq_arm_state {
	/* Keepalive-related data */
	struct task_struct *ka_thread;
	struct completion ka_evt;
	atomic_t ka_use_count;
	atomic_t ka_use_ack_count;
	atomic_t ka_release_count;

	rwlock_t susp_res_lock;

	struct vchiq_state *state;

	/*
	 * Global use count for videocore.
	 * This is equal to the sum of the use counts for all services.  When
	 * this hits zero the videocore suspend procedure will be initiated.
	 */
	int videocore_use_count;

	/*
	 * Use count to track requests from videocore peer.
	 * This use count is not associated with a service, so needs to be
	 * tracked separately with the state.
	 */
	int peer_use_count;

	/*
	 * Flag to indicate that the first vchiq connect has made it through.
	 * This means that both sides should be fully ready, and we should
	 * be able to suspend after this point.
	 */
	int first_connect;
};

static int
vchiq_blocking_bulk_transfer(struct vchiq_instance *instance, unsigned int handle, void *data,
			     unsigned int size, enum vchiq_bulk_dir dir);

static void
vchiq_arm_init_state(struct vchiq_state *state,
		     struct vchiq_arm_state *arm_state)
{
	if (arm_state) {
		rwlock_init(&arm_state->susp_res_lock);

		init_completion(&arm_state->ka_evt);
		atomic_set(&arm_state->ka_use_count, 0);
		atomic_set(&arm_state->ka_use_ack_count, 0);
		atomic_set(&arm_state->ka_release_count, 0);

		arm_state->state = state;
		arm_state->first_connect = 0;
	}
}

int
vchiq_platform_init_state(struct vchiq_state *state)
{
	struct vchiq_arm_state *platform_state;

	platform_state = devm_kzalloc(state->dev, sizeof(*platform_state), GFP_KERNEL);
	if (!platform_state)
		return -ENOMEM;

	vchiq_arm_init_state(state, platform_state);
	state->platform_state = (struct opaque_platform_state *)platform_state;

	return 0;
}

static struct vchiq_arm_state *vchiq_platform_get_arm_state(struct vchiq_state *state)
{
	return (struct vchiq_arm_state *)state->platform_state;
}


/*
 * Linux: writel(0, g_regs + BELL2) -- ring the VPU's doorbell. The register
 * write is in BCM2837VCHIQ.cpp, which owns the mapping.
 */
void
remote_event_signal(struct remote_event *event)
{
	/*
	 * Ensure that all writes to shared data structures have completed
	 * before signalling the peer.
	 */
	wmb();

	event->fired = 1;

	dsb(sy);         /* data barrier operation */

	if (event->armed)
		vchiq_xnu_ring_doorbell(); /* trigger vc interrupt */
}

/* Linux: create_pagelist()/free_pagelist(), BCM2837VCHIQ.cpp's here. */
int
vchiq_prepare_bulk_data(struct vchiq_instance *instance, struct vchiq_bulk *bulk, void *offset,
			void __user *uoffset, int size, int dir)
{
	void *cookie;
	u32 bus;

	if (vchiq_xnu_create_pagelist(offset, uoffset, size, dir == VCHIQ_BULK_RECEIVE,
				      &bus, &cookie))
		return -ENOMEM;

	bulk->data = bus;

	/*
	 * Store the pagelistinfo address in remote_data,
	 * which isn't used by the slave.
	 */
	bulk->remote_data = cookie;

	return 0;
}

void
vchiq_complete_bulk(struct vchiq_instance *instance, struct vchiq_bulk *bulk)
{
	if (bulk && bulk->remote_data && bulk->actual)
		vchiq_xnu_free_pagelist(bulk->remote_data, bulk->actual);
}

/* Linux: TOTAL_SLOTS, PAGE_ALIGN()ed to the VPU's 4 KiB */
#define TOTAL_SLOTS (VCHIQ_SLOT_ZERO_SLOTS + 2 * 32)

int
vchiq_xnu_slot_mem_size(void)
{
	return (TOTAL_SLOTS * VCHIQ_SLOT_SIZE + 4095) & ~4095;
}

/* Linux: the arm/core/core_msg/sync/susp_log_level module parameters. */
void
vchiq_xnu_set_log_level(int level)
{
	vchiq_arm_log_level = level;
	vchiq_core_log_level = level;
	vchiq_core_msg_log_level = level;
	vchiq_sync_log_level = level;
	vchiq_susp_log_level = level;
}

/* Linux: vchiq_doorbell_irq(), minus reading BELL0 (done by the caller). */
void
vchiq_xnu_doorbell(void)
{
	remote_event_pollall(&g_state);
}

/*
 * Linux: the middle of vchiq_platform_init(), between allocating the slots
 * and telling the firmware where they are.
 */
int
vchiq_xnu_init_state(void *slot_mem, int slot_mem_size, u32 fragments_bus, int fragments_count)
{
	struct vchiq_slot_zero *vchiq_slot_zero;

	vchiq_slot_zero = vchiq_init_slots(slot_mem, slot_mem_size);
	if (!vchiq_slot_zero)
		return -ENOMEM;

	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_OFFSET_IDX] =
		fragments_bus;
	vchiq_slot_zero->platform_data[VCHIQ_PLATFORM_FRAGMENTS_COUNT_IDX] =
		fragments_count;

	return vchiq_init_state(&g_state, vchiq_slot_zero, NULL);
}

int vchiq_dump_platform_state(void *dump_context)
{
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf), "  Platform: 2835 (VC master)");
	return vchiq_dump(dump_context, buf, len + 1);
}

#define VCHIQ_INIT_RETRIES 10
int vchiq_initialise(struct vchiq_instance **instance_out)
{
	struct vchiq_state *state;
	struct vchiq_instance *instance = NULL;
	int i, ret;

	/*
	 * VideoCore may not be ready due to boot up timing.
	 * It may never be ready if kernel and firmware are mismatched,so don't
	 * block forever.
	 */
	for (i = 0; i < VCHIQ_INIT_RETRIES; i++) {
		state = vchiq_get_state();
		if (state)
			break;
		usleep_range(500, 600);
	}
	if (i == VCHIQ_INIT_RETRIES) {
		vchiq_log_error(vchiq_core_log_level, "%s: videocore not initialized\n", __func__);
		ret = -ENOTCONN;
		goto failed;
	} else if (i > 0) {
		vchiq_log_warning(vchiq_core_log_level,
				  "%s: videocore initialized after %d retries\n", __func__, i);
	}

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance) {
		vchiq_log_error(vchiq_core_log_level,
				"%s: error allocating vchiq instance\n", __func__);
		ret = -ENOMEM;
		goto failed;
	}

	instance->connected = 0;
	instance->state = state;
	mutex_init(&instance->bulk_waiter_list_mutex);
	INIT_LIST_HEAD(&instance->bulk_waiter_list);

	*instance_out = instance;

	ret = 0;

failed:
	vchiq_log_trace(vchiq_core_log_level, "%s(%p): returning %d", __func__, instance, ret);

	return ret;
}
EXPORT_SYMBOL(vchiq_initialise);

void free_bulk_waiter(struct vchiq_instance *instance)
{
	struct bulk_waiter_node *waiter, *next;

	list_for_each_entry_safe(waiter, next,
				 &instance->bulk_waiter_list, list) {
		list_del(&waiter->list);
		vchiq_log_info(vchiq_arm_log_level, "bulk_waiter - cleaned up %pK for pid %d",
			       waiter, waiter->pid);
		kfree(waiter);
	}
}

int vchiq_shutdown(struct vchiq_instance *instance)
{
	int status = 0;
	struct vchiq_state *state = instance->state;

	if (mutex_lock_killable(&state->mutex))
		return -EAGAIN;

	/* Remove all services */
	vchiq_shutdown_internal(state, instance);

	mutex_unlock(&state->mutex);

	vchiq_log_trace(vchiq_core_log_level, "%s(%p): returning %d", __func__, instance, status);

	free_bulk_waiter(instance);
	kfree(instance);

	return status;
}
EXPORT_SYMBOL(vchiq_shutdown);

static int vchiq_is_connected(struct vchiq_instance *instance)
{
	return instance->connected;
}

int vchiq_connect(struct vchiq_instance *instance)
{
	int status;
	struct vchiq_state *state = instance->state;

	if (mutex_lock_killable(&state->mutex)) {
		vchiq_log_trace(vchiq_core_log_level, "%s: call to mutex_lock failed", __func__);
		status = -EAGAIN;
		goto failed;
	}
	status = vchiq_connect_internal(state, instance);

	if (!status)
		instance->connected = 1;

	mutex_unlock(&state->mutex);

failed:
	vchiq_log_trace(vchiq_core_log_level, "%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_connect);

static int
vchiq_add_service(struct vchiq_instance *instance,
		  const struct vchiq_service_params_kernel *params,
		  unsigned int *phandle)
{
	int status;
	struct vchiq_state *state = instance->state;
	struct vchiq_service *service = NULL;
	int srvstate;

	*phandle = VCHIQ_SERVICE_HANDLE_INVALID;

	srvstate = vchiq_is_connected(instance)
		? VCHIQ_SRVSTATE_LISTENING
		: VCHIQ_SRVSTATE_HIDDEN;

	service = vchiq_add_service_internal(state, params, srvstate, instance, NULL);

	if (service) {
		*phandle = service->handle;
		status = 0;
	} else {
		status = -EINVAL;
	}

	vchiq_log_trace(vchiq_core_log_level, "%s(%p): returning %d", __func__, instance, status);

	return status;
}

int
vchiq_open_service(struct vchiq_instance *instance,
		   const struct vchiq_service_params_kernel *params,
		   unsigned int *phandle)
{
	int status = -EINVAL;
	struct vchiq_state   *state = instance->state;
	struct vchiq_service *service = NULL;

	*phandle = VCHIQ_SERVICE_HANDLE_INVALID;

	if (!vchiq_is_connected(instance))
		goto failed;

	service = vchiq_add_service_internal(state, params, VCHIQ_SRVSTATE_OPENING, instance, NULL);

	if (service) {
		*phandle = service->handle;
		status = vchiq_open_service_internal(service, vchiq_xnu_current_tid());
		if (status) {
			vchiq_remove_service(instance, service->handle);
			*phandle = VCHIQ_SERVICE_HANDLE_INVALID;
		}
	}

failed:
	vchiq_log_trace(vchiq_core_log_level, "%s(%p): returning %d", __func__, instance, status);

	return status;
}
EXPORT_SYMBOL(vchiq_open_service);

int
vchiq_bulk_transmit(struct vchiq_instance *instance, unsigned int handle, const void *data,
		    unsigned int size, void *userdata, enum vchiq_bulk_mode mode)
{
	int status;

	while (1) {
		switch (mode) {
		case VCHIQ_BULK_MODE_NOCALLBACK:
		case VCHIQ_BULK_MODE_CALLBACK:
			status = vchiq_bulk_transfer(instance, handle,
						     (void *)data, NULL,
						     size, userdata, mode,
						     VCHIQ_BULK_TRANSMIT);
			break;
		case VCHIQ_BULK_MODE_BLOCKING:
			status = vchiq_blocking_bulk_transfer(instance, handle, (void *)data, size,
							      VCHIQ_BULK_TRANSMIT);
			break;
		default:
			return -EINVAL;
		}

		/*
		 * vchiq_*_bulk_transfer() may return -EAGAIN, so we need
		 * to implement a retry mechanism since this function is
		 * supposed to block until queued
		 */
		if (status != -EAGAIN)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_transmit);

int vchiq_bulk_receive(struct vchiq_instance *instance, unsigned int handle,
		       void *data, unsigned int size, void *userdata,
		       enum vchiq_bulk_mode mode)
{
	int status;

	while (1) {
		switch (mode) {
		case VCHIQ_BULK_MODE_NOCALLBACK:
		case VCHIQ_BULK_MODE_CALLBACK:
			status = vchiq_bulk_transfer(instance, handle, data, NULL,
						     size, userdata,
						     mode, VCHIQ_BULK_RECEIVE);
			break;
		case VCHIQ_BULK_MODE_BLOCKING:
			status = vchiq_blocking_bulk_transfer(instance, handle, (void *)data, size,
							      VCHIQ_BULK_RECEIVE);
			break;
		default:
			return -EINVAL;
		}

		/*
		 * vchiq_*_bulk_transfer() may return -EAGAIN, so we need
		 * to implement a retry mechanism since this function is
		 * supposed to block until queued
		 */
		if (status != -EAGAIN)
			break;

		msleep(1);
	}

	return status;
}
EXPORT_SYMBOL(vchiq_bulk_receive);

static int
vchiq_blocking_bulk_transfer(struct vchiq_instance *instance, unsigned int handle, void *data,
			     unsigned int size, enum vchiq_bulk_dir dir)
{
	struct vchiq_service *service;
	int status;
	struct bulk_waiter_node *waiter = NULL, *iter;

	service = find_service_by_handle(instance, handle);
	if (!service)
		return -EINVAL;

	vchiq_service_put(service);

	mutex_lock(&instance->bulk_waiter_list_mutex);
	list_for_each_entry(iter, &instance->bulk_waiter_list, list) {
		if (iter->pid == vchiq_xnu_current_tid()) {
			list_del(&iter->list);
			waiter = iter;
			break;
		}
	}
	mutex_unlock(&instance->bulk_waiter_list_mutex);

	if (waiter) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* This thread has an outstanding bulk transfer. */
			/* FIXME: why compare a dma address to a pointer? */
			if ((bulk->data != (dma_addr_t)(uintptr_t)data) || (bulk->size != size)) {
				/*
				 * This is not a retry of the previous one.
				 * Cancel the signal when the transfer completes.
				 */
				spin_lock(&bulk_waiter_spinlock);
				bulk->userdata = NULL;
				spin_unlock(&bulk_waiter_spinlock);
			}
		}
	} else {
		waiter = kzalloc(sizeof(*waiter), GFP_KERNEL);
		if (!waiter) {
			vchiq_log_error(vchiq_core_log_level, "%s - out of memory", __func__);
			return -ENOMEM;
		}
	}

	status = vchiq_bulk_transfer(instance, handle, data, NULL, size,
				     &waiter->bulk_waiter,
				     VCHIQ_BULK_MODE_BLOCKING, dir);
	if ((status != -EAGAIN) || fatal_signal_pending(current) || !waiter->bulk_waiter.bulk) {
		struct vchiq_bulk *bulk = waiter->bulk_waiter.bulk;

		if (bulk) {
			/* Cancel the signal when the transfer completes. */
			spin_lock(&bulk_waiter_spinlock);
			bulk->userdata = NULL;
			spin_unlock(&bulk_waiter_spinlock);
		}
		kfree(waiter);
	} else {
		waiter->pid = vchiq_xnu_current_tid();
		mutex_lock(&instance->bulk_waiter_list_mutex);
		list_add(&waiter->list, &instance->bulk_waiter_list);
		mutex_unlock(&instance->bulk_waiter_list_mutex);
		vchiq_log_info(vchiq_arm_log_level, "saved bulk_waiter %pK for pid %d", waiter,
			       vchiq_xnu_current_tid());
	}

	return status;
}

static int
add_completion(struct vchiq_instance *instance, enum vchiq_reason reason,
	       struct vchiq_header *header, struct user_service *user_service,
	       void *bulk_userdata)
{
	struct vchiq_completion_data_kernel *completion;
	int insert;

	DEBUG_INITIALISE(g_state.local);

	insert = instance->completion_insert;
	while ((insert - instance->completion_remove) >= MAX_COMPLETIONS) {
		/* Out of space - wait for the client */
		DEBUG_TRACE(SERVICE_CALLBACK_LINE);
		vchiq_log_trace(vchiq_arm_log_level, "%s - completion queue full", __func__);
		DEBUG_COUNT(COMPLETION_QUEUE_FULL_COUNT);
		if (wait_for_completion_interruptible(&instance->remove_event)) {
			vchiq_log_info(vchiq_arm_log_level, "service_callback interrupted");
			return -EAGAIN;
		} else if (instance->closing) {
			vchiq_log_info(vchiq_arm_log_level, "service_callback closing");
			return 0;
		}
		DEBUG_TRACE(SERVICE_CALLBACK_LINE);
	}

	completion = &instance->completions[insert & (MAX_COMPLETIONS - 1)];

	completion->header = header;
	completion->reason = reason;
	/* N.B. service_userdata is updated while processing AWAIT_COMPLETION */
	completion->service_userdata = user_service->service;
	completion->bulk_userdata = bulk_userdata;

	if (reason == VCHIQ_SERVICE_CLOSED) {
		/*
		 * Take an extra reference, to be held until
		 * this CLOSED notification is delivered.
		 */
		vchiq_service_get(user_service->service);
		if (instance->use_close_delivered)
			user_service->close_pending = 1;
	}

	/*
	 * A write barrier is needed here to ensure that the entire completion
	 * record is written out before the insert point.
	 */
	wmb();

	if (reason == VCHIQ_MESSAGE_AVAILABLE)
		user_service->message_available_pos = insert;

	insert++;
	instance->completion_insert = insert;

	complete(&instance->insert_event);

	return 0;
}

int
service_callback(struct vchiq_instance *instance, enum vchiq_reason reason,
		 struct vchiq_header *header, unsigned int handle, void *bulk_userdata)
{
	/*
	 * How do we ensure the callback goes to the right client?
	 * The service_user data points to a user_service record
	 * containing the original callback and the user state structure, which
	 * contains a circular buffer for completion records.
	 */
	struct user_service *user_service;
	struct vchiq_service *service;
	bool skip_completion = false;

	DEBUG_INITIALISE(g_state.local);

	DEBUG_TRACE(SERVICE_CALLBACK_LINE);

	rcu_read_lock();
	service = handle_to_service(instance, handle);
	if (WARN_ON(!service)) {
		rcu_read_unlock();
		return 0;
	}

	user_service = (struct user_service *)service->base.userdata;

	if (!instance || instance->closing) {
		rcu_read_unlock();
		return 0;
	}

	/*
	 * As hopping around different synchronization mechanism,
	 * taking an extra reference results in simpler implementation.
	 */
	vchiq_service_get(service);
	rcu_read_unlock();

	vchiq_log_trace(vchiq_arm_log_level,
			"%s - service %lx(%d,%p), reason %d, header %lx, instance %lx, bulk_userdata %lx",
			__func__, (unsigned long)user_service, service->localport,
			user_service->userdata, reason, (unsigned long)header,
			(unsigned long)instance, (unsigned long)bulk_userdata);

	if (header && user_service->is_vchi) {
		spin_lock(&msg_queue_spinlock);
		while (user_service->msg_insert ==
			(user_service->msg_remove + MSG_QUEUE_SIZE)) {
			spin_unlock(&msg_queue_spinlock);
			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			DEBUG_COUNT(MSG_QUEUE_FULL_COUNT);
			vchiq_log_trace(vchiq_arm_log_level, "%s - msg queue full", __func__);
			/*
			 * If there is no MESSAGE_AVAILABLE in the completion
			 * queue, add one
			 */
			if ((user_service->message_available_pos -
				instance->completion_remove) < 0) {
				int status;

				vchiq_log_info(vchiq_arm_log_level,
					       "Inserting extra MESSAGE_AVAILABLE");
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				status = add_completion(instance, reason, NULL, user_service,
							bulk_userdata);
				if (status) {
					DEBUG_TRACE(SERVICE_CALLBACK_LINE);
					vchiq_service_put(service);
					return status;
				}
			}

			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			if (wait_for_completion_interruptible(&user_service->remove_event)) {
				vchiq_log_info(vchiq_arm_log_level, "%s interrupted", __func__);
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				vchiq_service_put(service);
				return -EAGAIN;
			} else if (instance->closing) {
				vchiq_log_info(vchiq_arm_log_level, "%s closing", __func__);
				DEBUG_TRACE(SERVICE_CALLBACK_LINE);
				vchiq_service_put(service);
				return -EINVAL;
			}
			DEBUG_TRACE(SERVICE_CALLBACK_LINE);
			spin_lock(&msg_queue_spinlock);
		}

		user_service->msg_queue[user_service->msg_insert &
			(MSG_QUEUE_SIZE - 1)] = header;
		user_service->msg_insert++;

		/*
		 * If there is a thread waiting in DEQUEUE_MESSAGE, or if
		 * there is a MESSAGE_AVAILABLE in the completion queue then
		 * bypass the completion queue.
		 */
		if (((user_service->message_available_pos -
			instance->completion_remove) >= 0) ||
			user_service->dequeue_pending) {
			user_service->dequeue_pending = 0;
			skip_completion = true;
		}

		spin_unlock(&msg_queue_spinlock);
		complete(&user_service->insert_event);

		header = NULL;
	}
	DEBUG_TRACE(SERVICE_CALLBACK_LINE);
	vchiq_service_put(service);

	if (skip_completion)
		return 0;

	return add_completion(instance, reason, header, user_service,
		bulk_userdata);
}

int vchiq_dump(void *dump_context, const char *str, int len)
{
	struct dump_context *context = (struct dump_context *)dump_context;
	int copy_bytes;

	if (context->actual >= context->space)
		return 0;

	if (context->offset > 0) {
		int skip_bytes = min_t(int, len, context->offset);

		str += skip_bytes;
		len -= skip_bytes;
		context->offset -= skip_bytes;
		if (context->offset > 0)
			return 0;
	}
	copy_bytes = min_t(int, len, context->space - context->actual);
	if (copy_bytes == 0)
		return 0;
	if (copy_to_user(context->buf + context->actual, str,
			 copy_bytes))
		return -EFAULT;
	context->actual += copy_bytes;
	len -= copy_bytes;

	/*
	 * If the terminating NUL is included in the length, then it
	 * marks the end of a line and should be replaced with a
	 * carriage return.
	 */
	if ((len == 0) && (str[copy_bytes - 1] == '\0')) {
		char cr = '\n';

		if (copy_to_user(context->buf + context->actual - 1,
				 &cr, 1))
			return -EFAULT;
	}
	return 0;
}

int vchiq_dump_platform_instances(void *dump_context)
{
	struct vchiq_state *state = vchiq_get_state();
	char buf[80];
	int len;
	int i;

	if (!state)
		return -ENOTCONN;

	/*
	 * There is no list of instances, so instead scan all services,
	 * marking those that have been dumped.
	 */

	rcu_read_lock();
	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service;
		struct vchiq_instance *instance;

		service = rcu_dereference(state->services[i]);
		if (!service || service->base.callback != service_callback)
			continue;

		instance = service->instance;
		if (instance)
			instance->mark = 0;
	}
	rcu_read_unlock();

	for (i = 0; i < state->unused_service; i++) {
		struct vchiq_service *service;
		struct vchiq_instance *instance;
		int err;

		rcu_read_lock();
		service = rcu_dereference(state->services[i]);
		if (!service || service->base.callback != service_callback) {
			rcu_read_unlock();
			continue;
		}

		instance = service->instance;
		if (!instance || instance->mark) {
			rcu_read_unlock();
			continue;
		}
		rcu_read_unlock();

		len = snprintf(buf, sizeof(buf),
			       "Instance %pK: pid %d,%s completions %d/%d",
			       instance, instance->pid,
			       instance->connected ? " connected, " :
			       "",
			       instance->completion_insert -
			       instance->completion_remove,
			       MAX_COMPLETIONS);
		err = vchiq_dump(dump_context, buf, len + 1);
		if (err)
			return err;
		instance->mark = 1;
	}
	return 0;
}

int vchiq_dump_platform_service_state(void *dump_context,
				      struct vchiq_service *service)
{
	struct user_service *user_service =
			(struct user_service *)service->base.userdata;
	char buf[80];
	int len;

	len = scnprintf(buf, sizeof(buf), "  instance %pK", service->instance);

	if ((service->base.callback == service_callback) && user_service->is_vchi) {
		len += scnprintf(buf + len, sizeof(buf) - len, ", %d/%d messages",
				 user_service->msg_insert - user_service->msg_remove,
				 MSG_QUEUE_SIZE);

		if (user_service->dequeue_pending)
			len += scnprintf(buf + len, sizeof(buf) - len,
				" (dequeue pending)");
	}

	return vchiq_dump(dump_context, buf, len + 1);
}

struct vchiq_state *
vchiq_get_state(void)
{
	if (!g_state.remote) {
		pr_err("%s: g_state.remote == NULL\n", __func__);
		return NULL;
	}

	if (g_state.remote->initialised != 1) {
		pr_notice("%s: g_state.remote->initialised != 1 (%d)\n",
			  __func__, g_state.remote->initialised);
		return NULL;
	}

	return &g_state;
}

/*
 * Autosuspend related functionality
 */

static int
vchiq_keepalive_vchiq_callback(struct vchiq_instance *instance,
			       enum vchiq_reason reason,
			       struct vchiq_header *header,
			       unsigned int service_user, void *bulk_user)
{
	vchiq_log_error(vchiq_susp_log_level, "%s callback reason %d", __func__, reason);
	return 0;
}

static int
vchiq_keepalive_thread_func(void *v)
{
	struct vchiq_state *state = (struct vchiq_state *)v;
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	int status;
	struct vchiq_instance *instance;
	unsigned int ka_handle;
	int ret;

	struct vchiq_service_params_kernel params = {
		.fourcc      = VCHIQ_MAKE_FOURCC('K', 'E', 'E', 'P'),
		.callback    = vchiq_keepalive_vchiq_callback,
		.version     = KEEPALIVE_VER,
		.version_min = KEEPALIVE_VER_MIN
	};

	ret = vchiq_initialise(&instance);
	if (ret) {
		vchiq_log_error(vchiq_susp_log_level, "%s vchiq_initialise failed %d", __func__,
				ret);
		goto exit;
	}

	status = vchiq_connect(instance);
	if (status) {
		vchiq_log_error(vchiq_susp_log_level, "%s vchiq_connect failed %d", __func__,
				status);
		goto shutdown;
	}

	status = vchiq_add_service(instance, &params, &ka_handle);
	if (status) {
		vchiq_log_error(vchiq_susp_log_level, "%s vchiq_open_service failed %d", __func__,
				status);
		goto shutdown;
	}

	while (1) {
		long rc = 0, uc = 0;

		if (wait_for_completion_interruptible(&arm_state->ka_evt)) {
			vchiq_log_error(vchiq_susp_log_level, "%s interrupted", __func__);
			flush_signals(current);
			continue;
		}

		/*
		 * read and clear counters.  Do release_count then use_count to
		 * prevent getting more releases than uses
		 */
		rc = atomic_xchg(&arm_state->ka_release_count, 0);
		uc = atomic_xchg(&arm_state->ka_use_count, 0);

		/*
		 * Call use/release service the requisite number of times.
		 * Process use before release so use counts don't go negative
		 */
		while (uc--) {
			atomic_inc(&arm_state->ka_use_ack_count);
			status = vchiq_use_service(instance, ka_handle);
			if (status) {
				vchiq_log_error(vchiq_susp_log_level,
						"%s vchiq_use_service error %d", __func__, status);
			}
		}
		while (rc--) {
			status = vchiq_release_service(instance, ka_handle);
			if (status) {
				vchiq_log_error(vchiq_susp_log_level,
						"%s vchiq_release_service error %d", __func__,
						status);
			}
		}
	}

shutdown:
	vchiq_shutdown(instance);
exit:
	return 0;
}

int
vchiq_use_internal(struct vchiq_state *state, struct vchiq_service *service,
		   enum USE_TYPE_E use_type)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int ret = 0;
	char entity[16];
	int *entity_uc;
	int local_uc;

	if (!arm_state) {
		ret = -EINVAL;
		goto out;
	}

	if (use_type == USE_TYPE_VCHIQ) {
		sprintf(entity, "VCHIQ:   ");
		entity_uc = &arm_state->peer_use_count;
	} else if (service) {
		sprintf(entity, "%c%c%c%c:%03d",
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id);
		entity_uc = &service->service_use_count;
	} else {
		vchiq_log_error(vchiq_susp_log_level, "%s null service ptr", __func__);
		ret = -EINVAL;
		goto out;
	}

	write_lock_bh(&arm_state->susp_res_lock);
	local_uc = ++arm_state->videocore_use_count;
	++(*entity_uc);

	vchiq_log_trace(vchiq_susp_log_level, "%s %s count %d, state count %d", __func__, entity,
			*entity_uc, local_uc);

	write_unlock_bh(&arm_state->susp_res_lock);

	if (!ret) {
		int status = 0;
		long ack_cnt = atomic_xchg(&arm_state->ka_use_ack_count, 0);

		while (ack_cnt && !status) {
			/* Send the use notify to videocore */
			status = vchiq_send_remote_use_active(state);
			if (!status)
				ack_cnt--;
			else
				atomic_add(ack_cnt, &arm_state->ka_use_ack_count);
		}
	}

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, ret);
	return ret;
}

int
vchiq_release_internal(struct vchiq_state *state, struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	int ret = 0;
	char entity[16];
	int *entity_uc;

	if (!arm_state) {
		ret = -EINVAL;
		goto out;
	}

	if (service) {
		sprintf(entity, "%c%c%c%c:%03d",
			VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc),
			service->client_id);
		entity_uc = &service->service_use_count;
	} else {
		sprintf(entity, "PEER:   ");
		entity_uc = &arm_state->peer_use_count;
	}

	write_lock_bh(&arm_state->susp_res_lock);
	if (!arm_state->videocore_use_count || !(*entity_uc)) {
		/* Don't use BUG_ON - don't allow user thread to crash kernel */
		WARN_ON(!arm_state->videocore_use_count);
		WARN_ON(!(*entity_uc));
		ret = -EINVAL;
		goto unlock;
	}
	--arm_state->videocore_use_count;
	--(*entity_uc);

	vchiq_log_trace(vchiq_susp_log_level, "%s %s count %d, state count %d", __func__, entity,
			*entity_uc, arm_state->videocore_use_count);

unlock:
	write_unlock_bh(&arm_state->susp_res_lock);

out:
	vchiq_log_trace(vchiq_susp_log_level, "%s exit %d", __func__, ret);
	return ret;
}

void
vchiq_on_remote_use(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	atomic_inc(&arm_state->ka_use_count);
	complete(&arm_state->ka_evt);
}

void
vchiq_on_remote_release(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);

	atomic_inc(&arm_state->ka_release_count);
	complete(&arm_state->ka_evt);
}

int
vchiq_use_service_internal(struct vchiq_service *service)
{
	return vchiq_use_internal(service->state, service, USE_TYPE_SERVICE);
}

int
vchiq_release_service_internal(struct vchiq_service *service)
{
	return vchiq_release_internal(service->state, service);
}

struct vchiq_debugfs_node *
vchiq_instance_get_debugfs_node(struct vchiq_instance *instance)
{
	return &instance->debugfs_node;
}

int
vchiq_instance_get_use_count(struct vchiq_instance *instance)
{
	struct vchiq_service *service;
	int use_count = 0, i;

	i = 0;
	rcu_read_lock();
	while ((service = __next_service_by_instance(instance->state,
						     instance, &i)))
		use_count += service->service_use_count;
	rcu_read_unlock();
	return use_count;
}

int
vchiq_instance_get_pid(struct vchiq_instance *instance)
{
	return instance->pid;
}

int
vchiq_instance_get_trace(struct vchiq_instance *instance)
{
	return instance->trace;
}

void
vchiq_instance_set_trace(struct vchiq_instance *instance, int trace)
{
	struct vchiq_service *service;
	int i;

	i = 0;
	rcu_read_lock();
	while ((service = __next_service_by_instance(instance->state,
						     instance, &i)))
		service->trace = trace;
	rcu_read_unlock();
	instance->trace = (trace != 0);
}

int
vchiq_use_service(struct vchiq_instance *instance, unsigned int handle)
{
	int ret = -EINVAL;
	struct vchiq_service *service = find_service_by_handle(instance, handle);

	if (service) {
		ret = vchiq_use_internal(service->state, service, USE_TYPE_SERVICE);
		vchiq_service_put(service);
	}
	return ret;
}
EXPORT_SYMBOL(vchiq_use_service);

int
vchiq_release_service(struct vchiq_instance *instance, unsigned int handle)
{
	int ret = -EINVAL;
	struct vchiq_service *service = find_service_by_handle(instance, handle);

	if (service) {
		ret = vchiq_release_internal(service->state, service);
		vchiq_service_put(service);
	}
	return ret;
}
EXPORT_SYMBOL(vchiq_release_service);

struct service_data_struct {
	int fourcc;
	int clientid;
	int use_count;
};

void
vchiq_dump_service_use_state(struct vchiq_state *state)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	struct service_data_struct *service_data;
	int i, found = 0;
	/*
	 * If there's more than 64 services, only dump ones with
	 * non-zero counts
	 */
	int only_nonzero = 0;
	static const char *nz = "<-- preventing suspend";

	int peer_count;
	int vc_use_count;
	int active_services;

	if (!arm_state)
		return;

	service_data = kmalloc_array(MAX_SERVICES, sizeof(*service_data),
				     GFP_KERNEL);
	if (!service_data)
		return;

	read_lock_bh(&arm_state->susp_res_lock);
	peer_count = arm_state->peer_use_count;
	vc_use_count = arm_state->videocore_use_count;
	active_services = state->unused_service;
	if (active_services > MAX_SERVICES)
		only_nonzero = 1;

	rcu_read_lock();
	for (i = 0; i < active_services; i++) {
		struct vchiq_service *service_ptr =
			rcu_dereference(state->services[i]);

		if (!service_ptr)
			continue;

		if (only_nonzero && !service_ptr->service_use_count)
			continue;

		if (service_ptr->srvstate == VCHIQ_SRVSTATE_FREE)
			continue;

		service_data[found].fourcc = service_ptr->base.fourcc;
		service_data[found].clientid = service_ptr->client_id;
		service_data[found].use_count = service_ptr->service_use_count;
		found++;
		if (found >= MAX_SERVICES)
			break;
	}
	rcu_read_unlock();

	read_unlock_bh(&arm_state->susp_res_lock);

	if (only_nonzero)
		vchiq_log_warning(vchiq_susp_log_level, "Too many active services (%d). Only dumping up to first %d services with non-zero use-count",
				  active_services, found);

	for (i = 0; i < found; i++) {
		vchiq_log_warning(vchiq_susp_log_level, "----- %c%c%c%c:%d service count %d %s",
				  VCHIQ_FOURCC_AS_4CHARS(service_data[i].fourcc),
				  service_data[i].clientid, service_data[i].use_count,
				  service_data[i].use_count ? nz : "");
	}
	vchiq_log_warning(vchiq_susp_log_level, "----- VCHIQ use count %d", peer_count);
	vchiq_log_warning(vchiq_susp_log_level, "--- Overall vchiq instance use count %d",
			  vc_use_count);

	kfree(service_data);
}

int
vchiq_check_service(struct vchiq_service *service)
{
	struct vchiq_arm_state *arm_state;
	int ret = -EINVAL;

	if (!service || !service->state)
		goto out;

	arm_state = vchiq_platform_get_arm_state(service->state);

	read_lock_bh(&arm_state->susp_res_lock);
	if (service->service_use_count)
		ret = 0;
	read_unlock_bh(&arm_state->susp_res_lock);

	if (ret) {
		vchiq_log_error(vchiq_susp_log_level,
				"%s ERROR - %c%c%c%c:%d service count %d, state count %d", __func__,
				VCHIQ_FOURCC_AS_4CHARS(service->base.fourcc), service->client_id,
				service->service_use_count, arm_state->videocore_use_count);
		vchiq_dump_service_use_state(service->state);
	}
out:
	return ret;
}

void vchiq_platform_conn_state_changed(struct vchiq_state *state,
				       enum vchiq_connstate oldstate,
				       enum vchiq_connstate newstate)
{
	struct vchiq_arm_state *arm_state = vchiq_platform_get_arm_state(state);
	char threadname[16];

	vchiq_log_info(vchiq_susp_log_level, "%d: %s->%s", state->id,
		       get_conn_state_name(oldstate), get_conn_state_name(newstate));
	if (state->conn_state != VCHIQ_CONNSTATE_CONNECTED)
		return;

	write_lock_bh(&arm_state->susp_res_lock);
	if (arm_state->first_connect) {
		write_unlock_bh(&arm_state->susp_res_lock);
		return;
	}

	arm_state->first_connect = 1;
	write_unlock_bh(&arm_state->susp_res_lock);
	snprintf(threadname, sizeof(threadname), "vchiq-keep/%d",
		 state->id);
	arm_state->ka_thread = kthread_create(&vchiq_keepalive_thread_func,
					      (void *)state,
					      threadname);
	if (IS_ERR(arm_state->ka_thread)) {
		vchiq_log_error(vchiq_susp_log_level,
				"vchiq: FATAL: couldn't create thread %s",
				threadname);
	} else {
		wake_up_process(arm_state->ka_thread);
	}
}
