/* iig(DriverKit-99) generated from IOHIDEventService.iig */

#undef	IIG_IMPLEMENTATION
#define	IIG_IMPLEMENTATION 	IOHIDEventService.iig

#if KERNEL
#include <libkern/c++/OSString.h>
#else
#include <DriverKit/DriverKit.h>
#endif /* KERNEL */
#include <DriverKit/IOReturn.h>
#include <HIDDriverKit/IOHIDEventService.h>


#if __has_builtin(__builtin_load_member_function_pointer)
#define SimpleMemberFunctionCast(cfnty, self, func) (cfnty)__builtin_load_member_function_pointer(self, func)
#else
#define SimpleMemberFunctionCast(cfnty, self, func) ({ union { typeof(func) memfun; cfnty cfun; } pair; pair.memfun = func; pair.cfun; })
#endif


struct IOHIDEventService__Start_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  provider;
};
#pragma pack(4)
struct IOHIDEventService__Start_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t provider__descriptor;
    IOHIDEventService__Start_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService__Start_Msg_ObjRefs (2)

struct IOHIDEventService__Start_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService__Start_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService__Start_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService__Start_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService__Start_Msg * message;
        struct IOHIDEventService__Start_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService__Start_Invocation;
struct IOHIDEventService_EventAvailable_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint32_t  length;
};
#pragma pack(4)
struct IOHIDEventService_EventAvailable_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService_EventAvailable_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService_EventAvailable_Msg_ObjRefs (1)

struct IOHIDEventService_EventAvailable_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService_EventAvailable_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService_EventAvailable_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService_EventAvailable_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService_EventAvailable_Msg * message;
        struct IOHIDEventService_EventAvailable_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService_EventAvailable_Invocation;
struct IOHIDEventService_SetEventMemory_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  memory;
};
#pragma pack(4)
struct IOHIDEventService_SetEventMemory_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t memory__descriptor;
    IOHIDEventService_SetEventMemory_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService_SetEventMemory_Msg_ObjRefs (2)

struct IOHIDEventService_SetEventMemory_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService_SetEventMemory_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService_SetEventMemory_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService_SetEventMemory_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService_SetEventMemory_Msg * message;
        struct IOHIDEventService_SetEventMemory_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService_SetEventMemory_Invocation;
struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint64_t  timeStamp;
    IOFixed  dx;
    IOFixed  dy;
    IOFixed  dz;
    IOOptionBits  options;
    bool  accelerate;
};
#pragma pack(4)
struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg_ObjRefs (1)

struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg * message;
        struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService__DispatchRelativeScrollWheelEvent_Invocation;
struct IOHIDEventService__DispatchAbsolutePointerEvent_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint64_t  timeStamp;
    IOFixed  x;
    IOFixed  y;
    uint32_t  buttonState;
    IOOptionBits  options;
    bool  accelerate;
};
#pragma pack(4)
struct IOHIDEventService__DispatchAbsolutePointerEvent_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService__DispatchAbsolutePointerEvent_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchAbsolutePointerEvent_Msg_ObjRefs (1)

struct IOHIDEventService__DispatchAbsolutePointerEvent_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService__DispatchAbsolutePointerEvent_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService__DispatchAbsolutePointerEvent_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchAbsolutePointerEvent_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService__DispatchAbsolutePointerEvent_Msg * message;
        struct IOHIDEventService__DispatchAbsolutePointerEvent_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService__DispatchAbsolutePointerEvent_Invocation;
struct IOHIDEventService__DispatchRelativePointerEvent_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint64_t  timeStamp;
    IOFixed  dx;
    IOFixed  dy;
    uint32_t  buttonState;
    IOOptionBits  options;
    bool  accelerate;
};
#pragma pack(4)
struct IOHIDEventService__DispatchRelativePointerEvent_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService__DispatchRelativePointerEvent_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchRelativePointerEvent_Msg_ObjRefs (1)

struct IOHIDEventService__DispatchRelativePointerEvent_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService__DispatchRelativePointerEvent_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService__DispatchRelativePointerEvent_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchRelativePointerEvent_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService__DispatchRelativePointerEvent_Msg * message;
        struct IOHIDEventService__DispatchRelativePointerEvent_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService__DispatchRelativePointerEvent_Invocation;
struct IOHIDEventService__DispatchKeyboardEvent_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint64_t  timeStamp;
    uint32_t  usagePage;
    uint32_t  usage;
    uint32_t  value;
    IOOptionBits  options;
    bool  repeat;
};
#pragma pack(4)
struct IOHIDEventService__DispatchKeyboardEvent_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService__DispatchKeyboardEvent_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchKeyboardEvent_Msg_ObjRefs (1)

struct IOHIDEventService__DispatchKeyboardEvent_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService__DispatchKeyboardEvent_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService__DispatchKeyboardEvent_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService__DispatchKeyboardEvent_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService__DispatchKeyboardEvent_Msg * message;
        struct IOHIDEventService__DispatchKeyboardEvent_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService__DispatchKeyboardEvent_Invocation;
struct IOHIDEventService_SetLED_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    uint32_t  usage;
    bool  on;
};
#pragma pack(4)
struct IOHIDEventService_SetLED_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDEventService_SetLED_Msg_Content content;
};
#pragma pack()
#define IOHIDEventService_SetLED_Msg_ObjRefs (1)

struct IOHIDEventService_SetLED_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDEventService_SetLED_Rpl
{
    IORPCMessageMach           mach;
    IOHIDEventService_SetLED_Rpl_Content content;
};
#pragma pack()
#define IOHIDEventService_SetLED_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDEventService_SetLED_Msg * message;
        struct IOHIDEventService_SetLED_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDEventService_SetLED_Invocation;
#if !KERNEL
extern OSMetaClass * gOSContainerMetaClass;
extern OSMetaClass * gOSDataMetaClass;
extern OSMetaClass * gOSNumberMetaClass;
extern OSMetaClass * gOSBooleanMetaClass;
extern OSMetaClass * gOSDictionaryMetaClass;
extern OSMetaClass * gOSArrayMetaClass;
extern OSMetaClass * gIODispatchQueueMetaClass;
extern OSMetaClass * gOSStringMetaClass;
extern OSMetaClass * gIOMemoryDescriptorMetaClass;
extern OSMetaClass * gIOUserClientMetaClass;
extern OSMetaClass * gIOBufferMemoryDescriptorMetaClass;
extern OSMetaClass * gIOHIDEventMetaClass;
#endif /* !KERNEL */

#if !KERNEL

#define IOHIDEventService_QueueNames  ""

#define IOHIDEventService_MethodNames  ""

#define IOHIDEventServiceMetaClass_MethodNames  ""

struct OSClassDescription_IOHIDEventService_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 0];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(IOHIDEventService_QueueNames)];
    char               methodNames[sizeof(IOHIDEventService_MethodNames)];
    char               metaMethodNames[sizeof(IOHIDEventServiceMetaClass_MethodNames)];
};

const struct OSClassDescription_IOHIDEventService_t
OSClassDescription_IOHIDEventService =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_IOHIDEventService_t),
        .name                    = "IOHIDEventService",
        .superName               = "IOService",
        .flags                   = 1*kOSClassCanRemote,
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .queueNamesSize       = sizeof(IOHIDEventService_QueueNames),
        .methodNamesSize         = sizeof(IOHIDEventService_MethodNames),
        .metaMethodNamesSize     = sizeof(IOHIDEventServiceMetaClass_MethodNames),
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDEventService_t, methodOptions),
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_IOHIDEventService_t, metaMethodOptions),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDEventService_t, queueNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_IOHIDEventService_t, methodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_IOHIDEventService_t, metaMethodNames),
    },
    .methodOptions =
    {
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = IOHIDEventService_QueueNames,
    .methodNames     = IOHIDEventService_MethodNames,
    .metaMethodNames = IOHIDEventServiceMetaClass_MethodNames,
};

OSMetaClass * gIOHIDEventServiceMetaClass;

static kern_return_t
IOHIDEventService_New(OSMetaClass * instance);

const OSClassLoadInformation
IOHIDEventService_Class = 
{
    .version           = 1,
    .description       = &OSClassDescription_IOHIDEventService.base,
    .instanceSize      = sizeof(IOHIDEventService),

    .metaPointer       = &gIOHIDEventServiceMetaClass,
    .New               = &IOHIDEventService_New,
};

extern const void * const
gIOHIDEventService_Declaration;
const void * const
gIOHIDEventService_Declaration
__attribute__((visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip")))
    = &IOHIDEventService_Class;

static kern_return_t
IOHIDEventService_New(OSMetaClass * instance)
{
    if (!new(instance) IOHIDEventServiceMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
IOHIDEventServiceMetaClass::New(OSObject * instance)
{
    if (!new(instance) IOHIDEventService) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

kern_return_t
IOHIDEventService::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
IOHIDEventService::_Dispatch(IOHIDEventService * self, const IORPC rpc)
{
    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {
#if KERNEL
        case IOHIDEventService__Start_ID:
        {
            ret = IOHIDEventService::_Start_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::_Start_Handler, *self, &IOHIDEventService::_Start_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService_EventAvailable_ID:
        {
            ret = IOHIDEventService::EventAvailable_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::EventAvailable_Handler, *self, &IOHIDEventService::EventAvailable_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService_SetEventMemory_ID:
        {
            ret = IOHIDEventService::SetEventMemory_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::SetEventMemory_Handler, *self, &IOHIDEventService::SetEventMemory_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService__DispatchRelativeScrollWheelEvent_ID:
        {
            ret = IOHIDEventService::_DispatchRelativeScrollWheelEvent_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::_DispatchRelativeScrollWheelEvent_Handler, *self, &IOHIDEventService::_DispatchRelativeScrollWheelEvent_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService__DispatchAbsolutePointerEvent_ID:
        {
            ret = IOHIDEventService::_DispatchAbsolutePointerEvent_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::_DispatchAbsolutePointerEvent_Handler, *self, &IOHIDEventService::_DispatchAbsolutePointerEvent_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService__DispatchRelativePointerEvent_ID:
        {
            ret = IOHIDEventService::_DispatchRelativePointerEvent_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::_DispatchRelativePointerEvent_Handler, *self, &IOHIDEventService::_DispatchRelativePointerEvent_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDEventService__DispatchKeyboardEvent_ID:
        {
            ret = IOHIDEventService::_DispatchKeyboardEvent_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::_DispatchKeyboardEvent_Handler, *self, &IOHIDEventService::_DispatchKeyboardEvent_Impl));
            break;
        }
#endif /* !KERNEL */
        case IOHIDEventService_SetLED_ID:
        {
            ret = IOHIDEventService::SetLED_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDEventService::SetLED_Handler, *self, &IOHIDEventService::SetLED_Impl));
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
IOHIDEventService::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
IOHIDEventServiceMetaClass::Dispatch(const IORPC rpc)
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
IOHIDEventService::_Start(
        IOService * provider,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService__Start_Msg msg;
        struct
        {
            IOHIDEventService__Start_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService__Start_Msg * msg = &buf.msg;
    struct IOHIDEventService__Start_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService__Start_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService__Start_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService__Start_Msg_ObjRefs;
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
            if (rpl->content.__hdr.msgid                  != IOHIDEventService__Start_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService__Start_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::EventAvailable(
        uint32_t length,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService_EventAvailable_Msg msg;
        struct
        {
            IOHIDEventService_EventAvailable_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService_EventAvailable_Msg * msg = &buf.msg;
    struct IOHIDEventService_EventAvailable_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService_EventAvailable_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService_EventAvailable_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService_EventAvailable_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.length = length;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService_EventAvailable_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService_EventAvailable_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::SetEventMemory(
        IOBufferMemoryDescriptor * memory,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService_SetEventMemory_Msg msg;
        struct
        {
            IOHIDEventService_SetEventMemory_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService_SetEventMemory_Msg * msg = &buf.msg;
    struct IOHIDEventService_SetEventMemory_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService_SetEventMemory_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService_SetEventMemory_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService_SetEventMemory_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->memory__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.memory = (OSObjectRef) memory;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService_SetEventMemory_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService_SetEventMemory_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchRelativeScrollWheelEvent(
        uint64_t timeStamp,
        IOFixed dx,
        IOFixed dy,
        IOFixed dz,
        IOOptionBits options,
        bool accelerate,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg msg;
        struct
        {
            IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg * msg = &buf.msg;
    struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService__DispatchRelativeScrollWheelEvent_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timeStamp = timeStamp;

    msg->content.dx = dx;

    msg->content.dy = dy;

    msg->content.dz = dz;

    msg->content.options = options;

    msg->content.accelerate = accelerate;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService__DispatchRelativeScrollWheelEvent_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchAbsolutePointerEvent(
        uint64_t timeStamp,
        IOFixed x,
        IOFixed y,
        uint32_t buttonState,
        IOOptionBits options,
        bool accelerate,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService__DispatchAbsolutePointerEvent_Msg msg;
        struct
        {
            IOHIDEventService__DispatchAbsolutePointerEvent_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService__DispatchAbsolutePointerEvent_Msg * msg = &buf.msg;
    struct IOHIDEventService__DispatchAbsolutePointerEvent_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService__DispatchAbsolutePointerEvent_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService__DispatchAbsolutePointerEvent_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService__DispatchAbsolutePointerEvent_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timeStamp = timeStamp;

    msg->content.x = x;

    msg->content.y = y;

    msg->content.buttonState = buttonState;

    msg->content.options = options;

    msg->content.accelerate = accelerate;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService__DispatchAbsolutePointerEvent_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService__DispatchAbsolutePointerEvent_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchRelativePointerEvent(
        uint64_t timeStamp,
        IOFixed dx,
        IOFixed dy,
        uint32_t buttonState,
        IOOptionBits options,
        bool accelerate,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService__DispatchRelativePointerEvent_Msg msg;
        struct
        {
            IOHIDEventService__DispatchRelativePointerEvent_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService__DispatchRelativePointerEvent_Msg * msg = &buf.msg;
    struct IOHIDEventService__DispatchRelativePointerEvent_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService__DispatchRelativePointerEvent_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService__DispatchRelativePointerEvent_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService__DispatchRelativePointerEvent_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timeStamp = timeStamp;

    msg->content.dx = dx;

    msg->content.dy = dy;

    msg->content.buttonState = buttonState;

    msg->content.options = options;

    msg->content.accelerate = accelerate;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService__DispatchRelativePointerEvent_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService__DispatchRelativePointerEvent_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchKeyboardEvent(
        uint64_t timeStamp,
        uint32_t usagePage,
        uint32_t usage,
        uint32_t value,
        IOOptionBits options,
        bool repeat,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService__DispatchKeyboardEvent_Msg msg;
        struct
        {
            IOHIDEventService__DispatchKeyboardEvent_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDEventService__DispatchKeyboardEvent_Msg * msg = &buf.msg;
    struct IOHIDEventService__DispatchKeyboardEvent_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDEventService__DispatchKeyboardEvent_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService__DispatchKeyboardEvent_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService__DispatchKeyboardEvent_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timeStamp = timeStamp;

    msg->content.usagePage = usagePage;

    msg->content.usage = usage;

    msg->content.value = value;

    msg->content.options = options;

    msg->content.repeat = repeat;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDEventService__DispatchKeyboardEvent_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDEventService__DispatchKeyboardEvent_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

void
IOHIDEventService::SetLED(
        uint32_t usage,
        bool on,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDEventService_SetLED_Msg msg;
    } buf;
    struct IOHIDEventService_SetLED_Msg * msg = &buf.msg;

    memset(msg, 0, sizeof(struct IOHIDEventService_SetLED_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 1*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDEventService_SetLED_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDEventService_SetLED_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.usage = usage;

    msg->content.on = on;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(*msg), .reply = NULL, .replySize = 0 };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

}

kern_return_t
IOHIDEventService::_Start_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _Start_Handler func)
{
    IOHIDEventService__Start_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOService * provider;

    if (IOHIDEventService__Start_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    provider = OSDynamicCast(IOService, (OSObject *) rpc.message->content.provider);
    if (!provider && rpc.message->content.provider) return (kIOReturnBadArgument);

    ret = (*func)(target,
        provider);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService__Start_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService__Start_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::EventAvailable_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        EventAvailable_Handler func)
{
    IOHIDEventService_EventAvailable_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDEventService_EventAvailable_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        rpc.message->content.length);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService_EventAvailable_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService_EventAvailable_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::SetEventMemory_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        SetEventMemory_Handler func)
{
    IOHIDEventService_SetEventMemory_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOBufferMemoryDescriptor * memory;

    if (IOHIDEventService_SetEventMemory_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    memory = OSDynamicCast(IOBufferMemoryDescriptor, (OSObject *) rpc.message->content.memory);
    if (!memory && rpc.message->content.memory) return (kIOReturnBadArgument);

    ret = (*func)(target,
        memory);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService_SetEventMemory_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService_SetEventMemory_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchRelativeScrollWheelEvent_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _DispatchRelativeScrollWheelEvent_Handler func)
{
    IOHIDEventService__DispatchRelativeScrollWheelEvent_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDEventService__DispatchRelativeScrollWheelEvent_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        rpc.message->content.timeStamp,
        rpc.message->content.dx,
        rpc.message->content.dy,
        rpc.message->content.dz,
        rpc.message->content.options,
        rpc.message->content.accelerate);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService__DispatchRelativeScrollWheelEvent_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService__DispatchRelativeScrollWheelEvent_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchAbsolutePointerEvent_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _DispatchAbsolutePointerEvent_Handler func)
{
    IOHIDEventService__DispatchAbsolutePointerEvent_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDEventService__DispatchAbsolutePointerEvent_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        rpc.message->content.timeStamp,
        rpc.message->content.x,
        rpc.message->content.y,
        rpc.message->content.buttonState,
        rpc.message->content.options,
        rpc.message->content.accelerate);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService__DispatchAbsolutePointerEvent_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService__DispatchAbsolutePointerEvent_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchRelativePointerEvent_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _DispatchRelativePointerEvent_Handler func)
{
    IOHIDEventService__DispatchRelativePointerEvent_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDEventService__DispatchRelativePointerEvent_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        rpc.message->content.timeStamp,
        rpc.message->content.dx,
        rpc.message->content.dy,
        rpc.message->content.buttonState,
        rpc.message->content.options,
        rpc.message->content.accelerate);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService__DispatchRelativePointerEvent_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService__DispatchRelativePointerEvent_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::_DispatchKeyboardEvent_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        _DispatchKeyboardEvent_Handler func)
{
    IOHIDEventService__DispatchKeyboardEvent_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDEventService__DispatchKeyboardEvent_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        rpc.message->content.timeStamp,
        rpc.message->content.usagePage,
        rpc.message->content.usage,
        rpc.message->content.value,
        rpc.message->content.options,
        rpc.message->content.repeat);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDEventService__DispatchKeyboardEvent_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDEventService__DispatchKeyboardEvent_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDEventService::SetLED_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        SetLED_Handler func)
{
    IOHIDEventService_SetLED_Invocation rpc = { _rpc };

    if (IOHIDEventService_SetLED_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    (*func)(target,
        rpc.message->content.usage,
        rpc.message->content.on);


    return (kIOReturnSuccess);
}



