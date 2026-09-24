/* iig(DriverKit-99) generated from IOHIDDevice.iig */

#undef	IIG_IMPLEMENTATION
#define	IIG_IMPLEMENTATION 	IOHIDDevice.iig

#if KERNEL
#include <libkern/c++/OSString.h>
#else
#include <DriverKit/DriverKit.h>
#endif /* KERNEL */
#include <DriverKit/IOReturn.h>
#include <HIDDriverKit/IOHIDDevice.h>


#if __has_builtin(__builtin_load_member_function_pointer)
#define SimpleMemberFunctionCast(cfnty, self, func) (cfnty)__builtin_load_member_function_pointer(self, func)
#else
#define SimpleMemberFunctionCast(cfnty, self, func) ({ union { typeof(func) memfun; cfnty cfun; } pair; pair.memfun = func; pair.cfun; })
#endif


struct IOHIDDevice__Start_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  provider;
};
#pragma pack(4)
struct IOHIDDevice__Start_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t provider__descriptor;
    IOHIDDevice__Start_Msg_Content content;
};
#pragma pack()
#define IOHIDDevice__Start_Msg_ObjRefs (2)

struct IOHIDDevice__Start_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDDevice__Start_Rpl
{
    IORPCMessageMach           mach;
    IOHIDDevice__Start_Rpl_Content content;
};
#pragma pack()
#define IOHIDDevice__Start_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDDevice__Start_Msg * message;
        struct IOHIDDevice__Start_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDDevice__Start_Invocation;
struct IOHIDDevice__ProcessReport_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
    OSObjectRef  action;
    HIDReportCommandType  command;
    IOHIDReportType  reportType;
    IOOptionBits  options;
    uint32_t  completionTimeout;
};
#pragma pack(4)
struct IOHIDDevice__ProcessReport_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    mach_msg_port_descriptor_t action__descriptor;
    IOHIDDevice__ProcessReport_Msg_Content content;
};
#pragma pack()
#define IOHIDDevice__ProcessReport_Msg_ObjRefs (3)

struct IOHIDDevice__ProcessReport_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDDevice__ProcessReport_Rpl
{
    IORPCMessageMach           mach;
    IOHIDDevice__ProcessReport_Rpl_Content content;
};
#pragma pack()
#define IOHIDDevice__ProcessReport_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDDevice__ProcessReport_Msg * message;
        struct IOHIDDevice__ProcessReport_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDDevice__ProcessReport_Invocation;
struct IOHIDDevice__HandleReport_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
    uint64_t  timestamp;
    uint32_t  reportLength;
    IOHIDReportType  reportType;
    IOOptionBits  options;
};
#pragma pack(4)
struct IOHIDDevice__HandleReport_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    IOHIDDevice__HandleReport_Msg_Content content;
};
#pragma pack()
#define IOHIDDevice__HandleReport_Msg_ObjRefs (2)

struct IOHIDDevice__HandleReport_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDDevice__HandleReport_Rpl
{
    IORPCMessageMach           mach;
    IOHIDDevice__HandleReport_Rpl_Content content;
};
#pragma pack()
#define IOHIDDevice__HandleReport_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDDevice__HandleReport_Msg * message;
        struct IOHIDDevice__HandleReport_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDDevice__HandleReport_Invocation;
struct IOHIDDevice__SetProperty_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  serialization;
};
#pragma pack(4)
struct IOHIDDevice__SetProperty_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t serialization__descriptor;
    IOHIDDevice__SetProperty_Msg_Content content;
};
#pragma pack()
#define IOHIDDevice__SetProperty_Msg_ObjRefs (2)

struct IOHIDDevice__SetProperty_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDDevice__SetProperty_Rpl
{
    IORPCMessageMach           mach;
    IOHIDDevice__SetProperty_Rpl_Content content;
};
#pragma pack()
#define IOHIDDevice__SetProperty_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDDevice__SetProperty_Msg * message;
        struct IOHIDDevice__SetProperty_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDDevice__SetProperty_Invocation;
struct IOHIDDevice_CompleteReport_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  action;
    IOReturn  status;
    uint32_t  actualByteCount;
};
#pragma pack(4)
struct IOHIDDevice_CompleteReport_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t action__descriptor;
    IOHIDDevice_CompleteReport_Msg_Content content;
};
#pragma pack()
#define IOHIDDevice_CompleteReport_Msg_ObjRefs (2)

struct IOHIDDevice_CompleteReport_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDDevice_CompleteReport_Rpl
{
    IORPCMessageMach           mach;
    IOHIDDevice_CompleteReport_Rpl_Content content;
};
#pragma pack()
#define IOHIDDevice_CompleteReport_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDDevice_CompleteReport_Msg * message;
        struct IOHIDDevice_CompleteReport_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDDevice_CompleteReport_Invocation;
#if !KERNEL
extern OSMetaClass * gOSContainerMetaClass;
extern OSMetaClass * gOSDataMetaClass;
extern OSMetaClass * gOSNumberMetaClass;
extern OSMetaClass * gOSBooleanMetaClass;
extern OSMetaClass * gOSDictionaryMetaClass;
extern OSMetaClass * gOSArrayMetaClass;
extern OSMetaClass * gIODispatchQueueMetaClass;
extern OSMetaClass * gOSStringMetaClass;
extern OSMetaClass * gIOBufferMemoryDescriptorMetaClass;
extern OSMetaClass * gIOUserClientMetaClass;
extern OSMetaClass * gIOMemoryDescriptorMetaClass;
extern OSMetaClass * gOSAction_IOHIDDevice__CompleteReportMetaClass;
#endif /* !KERNEL */

#if !KERNEL

#define IOHIDDevice_QueueNames  ""

#define IOHIDDevice_MethodNames  ""

#define IOHIDDeviceMetaClass_MethodNames  ""

struct OSClassDescription_IOHIDDevice_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 0];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(IOHIDDevice_QueueNames)];
    char               methodNames[sizeof(IOHIDDevice_MethodNames)];
    char               metaMethodNames[sizeof(IOHIDDeviceMetaClass_MethodNames)];
};

const struct OSClassDescription_IOHIDDevice_t
OSClassDescription_IOHIDDevice =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_IOHIDDevice_t),
        .name                    = "IOHIDDevice",
        .superName               = "IOService",
        .flags                   = 1*kOSClassCanRemote,
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .queueNamesSize       = sizeof(IOHIDDevice_QueueNames),
        .methodNamesSize         = sizeof(IOHIDDevice_MethodNames),
        .metaMethodNamesSize     = sizeof(IOHIDDeviceMetaClass_MethodNames),
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDDevice_t, methodOptions),
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_IOHIDDevice_t, metaMethodOptions),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDDevice_t, queueNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_IOHIDDevice_t, methodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_IOHIDDevice_t, metaMethodNames),
    },
    .methodOptions =
    {
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = IOHIDDevice_QueueNames,
    .methodNames     = IOHIDDevice_MethodNames,
    .metaMethodNames = IOHIDDeviceMetaClass_MethodNames,
};

OSMetaClass * gIOHIDDeviceMetaClass;

static kern_return_t
IOHIDDevice_New(OSMetaClass * instance);

const OSClassLoadInformation
IOHIDDevice_Class = 
{
    .version           = 1,
    .description       = &OSClassDescription_IOHIDDevice.base,
    .instanceSize      = sizeof(IOHIDDevice),

    .metaPointer       = &gIOHIDDeviceMetaClass,
    .New               = &IOHIDDevice_New,
};

extern const void * const
gIOHIDDevice_Declaration;
const void * const
gIOHIDDevice_Declaration
__attribute__((visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip")))
    = &IOHIDDevice_Class;

static kern_return_t
IOHIDDevice_New(OSMetaClass * instance)
{
    if (!new(instance) IOHIDDeviceMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
IOHIDDeviceMetaClass::New(OSObject * instance)
{
    if (!new(instance) IOHIDDevice) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

kern_return_t
IOHIDDevice::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
IOHIDDevice::_Dispatch(IOHIDDevice * self, const IORPC rpc)
{
    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {
#if KERNEL
        case IOHIDDevice__Start_ID:
        {
            ret = IOHIDDevice::_Start_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDDevice::_Start_Handler, *self, &IOHIDDevice::_Start_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDDevice__CompleteReport_ID:
        {
            ret = IOHIDDevice::CompleteReport_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDDevice::CompleteReport_Handler, *self, &IOHIDDevice::_CompleteReport_Impl), OSTypeID(OSAction_IOHIDDevice__CompleteReport));
            break;
        }
#endif /* !KERNEL */
        case IOHIDDevice__ProcessReport_ID:
        {
            ret = IOHIDDevice::_ProcessReport_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDDevice::_ProcessReport_Handler, *self, &IOHIDDevice::_ProcessReport_Impl));
            break;
        }
#if KERNEL
        case IOHIDDevice__HandleReport_ID:
        {
            ret = IOHIDDevice::_HandleReport_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDDevice::_HandleReport_Handler, *self, &IOHIDDevice::_HandleReport_Impl));
            break;
        }
#endif /* !KERNEL */
        case IOHIDDevice__SetProperty_ID:
        {
            ret = IOHIDDevice::_SetProperty_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDDevice::_SetProperty_Handler, *self, &IOHIDDevice::_SetProperty_Impl));
            break;
        }

        default:
            ret = IOService::_Dispatch(self, rpc);
            break;
    }

    return (ret);
}

#if KERNEL
kern_return_t
IOHIDDevice::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
IOHIDDeviceMetaClass::Dispatch(const IORPC rpc)
{
#endif /* !KERNEL */

    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {

        default:
            ret = OSMetaClassBase::Dispatch(rpc);
            break;
    }

    return (ret);
}

kern_return_t
IOHIDDevice::_Start(
        IOService * provider,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDDevice__Start_Msg msg;
        struct
        {
            IOHIDDevice__Start_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDDevice__Start_Msg * msg = &buf.msg;
    struct IOHIDDevice__Start_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDDevice__Start_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDDevice__Start_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDDevice__Start_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->provider__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.provider = (OSObjectRef) provider;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDDevice__Start_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDDevice__Start_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDDevice::CreateAction_CompleteReport(size_t referenceSize, OSAction ** action)
{
    kern_return_t ret;

#if defined(IOKIT_ENABLE_SHARED_PTR)
    OSSharedPtr<OSString>
#else /* defined(IOKIT_ENABLE_SHARED_PTR) */
    OSString *
#endif /* !defined(IOKIT_ENABLE_SHARED_PTR) */
    typeName = OSString::withCString("OSAction_IOHIDDevice__CompleteReport");
    if (!typeName) {
        return kIOReturnNoMemory;
    }
    ret = OSAction_IOHIDDevice__CompleteReport::CreateWithTypeName(this,
                           IOHIDDevice__CompleteReport_ID,
                           IOHIDDevice_CompleteReport_ID,
                           referenceSize,
#if defined(IOKIT_ENABLE_SHARED_PTR)
                           typeName.get(),
#else /* defined(IOKIT_ENABLE_SHARED_PTR) */
                           typeName,
#endif /* !defined(IOKIT_ENABLE_SHARED_PTR) */
                           action);

#if !defined(IOKIT_ENABLE_SHARED_PTR)
    typeName->release();
#endif /* !defined(IOKIT_ENABLE_SHARED_PTR) */
    return (ret);
}

void
IOHIDDevice::_ProcessReport(
        HIDReportCommandType command,
        IOMemoryDescriptor * report,
        IOHIDReportType reportType,
        IOOptionBits options,
        uint32_t completionTimeout,
        OSAction * action,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDDevice__ProcessReport_Msg msg;
    } buf;
    struct IOHIDDevice__ProcessReport_Msg * msg = &buf.msg;

    memset(msg, 0, sizeof(struct IOHIDDevice__ProcessReport_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 1*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDDevice__ProcessReport_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDDevice__ProcessReport_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 3;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.command = command;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    msg->content.reportType = reportType;

    msg->content.options = options;

    msg->content.completionTimeout = completionTimeout;

    msg->action__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.action = (OSObjectRef) action;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(*msg), .reply = NULL, .replySize = 0 };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

}

kern_return_t
IOHIDDevice::_HandleReport(
        uint64_t timestamp,
        IOMemoryDescriptor * report,
        uint32_t reportLength,
        IOHIDReportType reportType,
        IOOptionBits options,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDDevice__HandleReport_Msg msg;
        struct
        {
            IOHIDDevice__HandleReport_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDDevice__HandleReport_Msg * msg = &buf.msg;
    struct IOHIDDevice__HandleReport_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDDevice__HandleReport_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDDevice__HandleReport_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDDevice__HandleReport_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timestamp = timestamp;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    msg->content.reportLength = reportLength;

    msg->content.reportType = reportType;

    msg->content.options = options;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDDevice__HandleReport_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDDevice__HandleReport_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

void
IOHIDDevice::_SetProperty(
        IOBufferMemoryDescriptor * serialization,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDDevice__SetProperty_Msg msg;
    } buf;
    struct IOHIDDevice__SetProperty_Msg * msg = &buf.msg;

    memset(msg, 0, sizeof(struct IOHIDDevice__SetProperty_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 1*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDDevice__SetProperty_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDDevice__SetProperty_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->serialization__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.serialization = (OSObjectRef) serialization;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(*msg), .reply = NULL, .replySize = 0 };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

}

void
IOHIDDevice::CompleteReport(
        OSAction * action,
        IOReturn status,
        uint32_t actualByteCount,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDDevice_CompleteReport_Msg msg;
    } buf;
    struct IOHIDDevice_CompleteReport_Msg * msg = &buf.msg;

    memset(msg, 0, sizeof(struct IOHIDDevice_CompleteReport_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 1*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDDevice_CompleteReport_ID;
    msg->content.__object = (OSObjectRef) action;
    msg->content.__hdr.objectRefs = IOHIDDevice_CompleteReport_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->action__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.action = (OSObjectRef) action;

    msg->content.status = status;

    msg->content.actualByteCount = actualByteCount;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(*msg), .reply = NULL, .replySize = 0 };
    ret = action->Invoke(rpc);

}

kern_return_t
IOHIDDevice::_Start_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _Start_Handler func)
{
    IOHIDDevice__Start_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOService * provider;

    if (IOHIDDevice__Start_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    provider = OSDynamicCast(IOService, (OSObject *) rpc.message->content.provider);
    if (!provider && rpc.message->content.provider) return (kIOReturnBadArgument);

    ret = (*func)(target,
        provider);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDDevice__Start_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDDevice__Start_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDDevice::_ProcessReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _ProcessReport_Handler func)
{
    IOHIDDevice__ProcessReport_Invocation rpc = { _rpc };
    IOMemoryDescriptor * report;
    OSAction * action;

    if (IOHIDDevice__ProcessReport_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);
    action = OSDynamicCast(OSAction, (OSObject *) rpc.message->content.action);
    if (!action && rpc.message->content.action) return (kIOReturnBadArgument);

    (*func)(target,
        rpc.message->content.command,
        report,
        rpc.message->content.reportType,
        rpc.message->content.options,
        rpc.message->content.completionTimeout,
        action);


    return (kIOReturnSuccess);
}

kern_return_t
IOHIDDevice::_HandleReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _HandleReport_Handler func)
{
    IOHIDDevice__HandleReport_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOMemoryDescriptor * report;

    if (IOHIDDevice__HandleReport_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);

    ret = (*func)(target,
        rpc.message->content.timestamp,
        report,
        rpc.message->content.reportLength,
        rpc.message->content.reportType,
        rpc.message->content.options);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDDevice__HandleReport_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDDevice__HandleReport_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDDevice::_SetProperty_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _SetProperty_Handler func)
{
    IOHIDDevice__SetProperty_Invocation rpc = { _rpc };
    IOBufferMemoryDescriptor * serialization;

    if (IOHIDDevice__SetProperty_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    serialization = OSDynamicCast(IOBufferMemoryDescriptor, (OSObject *) rpc.message->content.serialization);
    if (!serialization && rpc.message->content.serialization) return (kIOReturnBadArgument);

    (*func)(target,
        serialization);


    return (kIOReturnSuccess);
}

kern_return_t
IOHIDDevice::CompleteReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        CompleteReport_Handler func)
{
    return IOHIDDevice::CompleteReport_Invoke(_rpc, target, func, NULL);
}

kern_return_t
IOHIDDevice::CompleteReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        CompleteReport_Handler func,
        const OSMetaClass * targetActionClass)
{
    IOHIDDevice_CompleteReport_Invocation rpc = { _rpc };
    OSAction * action;

    if (IOHIDDevice_CompleteReport_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    if (targetActionClass) {
        action = (OSAction *) OSMetaClassBase::safeMetaCast((OSObject *) rpc.message->content.action, targetActionClass);
    } else {
        action = OSDynamicCast(OSAction, (OSObject *) rpc.message->content.action);
    }
    if (!action && rpc.message->content.action) return (kIOReturnBadArgument);

    (*func)(target,
        action,
        rpc.message->content.status,
        rpc.message->content.actualByteCount);


    return (kIOReturnSuccess);
}

#if KERNEL
OSDefineMetaClassAndStructors(OSAction_IOHIDDevice__CompleteReport, OSAction);
#endif /* KERNEL */

#if !KERNEL

#define OSAction_IOHIDDevice__CompleteReport_QueueNames  ""

#define OSAction_IOHIDDevice__CompleteReport_MethodNames  ""

#define OSAction_IOHIDDevice__CompleteReportMetaClass_MethodNames  ""

struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 0];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(OSAction_IOHIDDevice__CompleteReport_QueueNames)];
    char               methodNames[sizeof(OSAction_IOHIDDevice__CompleteReport_MethodNames)];
    char               metaMethodNames[sizeof(OSAction_IOHIDDevice__CompleteReportMetaClass_MethodNames)];
};

const struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t
OSClassDescription_OSAction_IOHIDDevice__CompleteReport =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t),
        .name                    = "OSAction_IOHIDDevice__CompleteReport",
        .superName               = "OSAction",
        .flags                   = 0*kOSClassCanRemote,
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .queueNamesSize       = sizeof(OSAction_IOHIDDevice__CompleteReport_QueueNames),
        .methodNamesSize         = sizeof(OSAction_IOHIDDevice__CompleteReport_MethodNames),
        .metaMethodNamesSize     = sizeof(OSAction_IOHIDDevice__CompleteReportMetaClass_MethodNames),
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t, methodOptions),
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t, metaMethodOptions),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t, queueNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t, methodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_OSAction_IOHIDDevice__CompleteReport_t, metaMethodNames),
    },
    .methodOptions =
    {
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = OSAction_IOHIDDevice__CompleteReport_QueueNames,
    .methodNames     = OSAction_IOHIDDevice__CompleteReport_MethodNames,
    .metaMethodNames = OSAction_IOHIDDevice__CompleteReportMetaClass_MethodNames,
};

OSMetaClass * gOSAction_IOHIDDevice__CompleteReportMetaClass;

static kern_return_t
OSAction_IOHIDDevice__CompleteReport_New(OSMetaClass * instance);

const OSClassLoadInformation
OSAction_IOHIDDevice__CompleteReport_Class = 
{
    .version           = 1,
    .description       = &OSClassDescription_OSAction_IOHIDDevice__CompleteReport.base,
    .instanceSize      = sizeof(OSAction_IOHIDDevice__CompleteReport),

    .metaPointer       = &gOSAction_IOHIDDevice__CompleteReportMetaClass,
    .New               = &OSAction_IOHIDDevice__CompleteReport_New,
};

extern const void * const
gOSAction_IOHIDDevice__CompleteReport_Declaration;
const void * const
gOSAction_IOHIDDevice__CompleteReport_Declaration
__attribute__((visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip")))
    = &OSAction_IOHIDDevice__CompleteReport_Class;

static kern_return_t
OSAction_IOHIDDevice__CompleteReport_New(OSMetaClass * instance)
{
    if (!new(instance) OSAction_IOHIDDevice__CompleteReportMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
OSAction_IOHIDDevice__CompleteReportMetaClass::New(OSObject * instance)
{
    if (!new(instance) OSAction_IOHIDDevice__CompleteReport) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

kern_return_t
OSAction_IOHIDDevice__CompleteReport::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
OSAction_IOHIDDevice__CompleteReport::_Dispatch(OSAction_IOHIDDevice__CompleteReport * self, const IORPC rpc)
{
    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {

        default:
            ret = OSAction::_Dispatch(self, rpc);
            break;
    }

    return (ret);
}

#if KERNEL
kern_return_t
OSAction_IOHIDDevice__CompleteReport::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
OSAction_IOHIDDevice__CompleteReportMetaClass::Dispatch(const IORPC rpc)
{
#endif /* !KERNEL */

    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {

        default:
            ret = OSMetaClassBase::Dispatch(rpc);
            break;
    }

    return (ret);
}



