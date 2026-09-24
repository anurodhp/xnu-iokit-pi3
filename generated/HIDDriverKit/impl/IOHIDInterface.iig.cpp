/* iig(DriverKit-99) generated from IOHIDInterface.iig */

#undef	IIG_IMPLEMENTATION
#define	IIG_IMPLEMENTATION 	IOHIDInterface.iig

#if KERNEL
#include <libkern/c++/OSString.h>
#else
#include <DriverKit/DriverKit.h>
#endif /* KERNEL */
#include <DriverKit/IOReturn.h>
#include <HIDDriverKit/IOHIDInterface.h>


#if __has_builtin(__builtin_load_member_function_pointer)
#define SimpleMemberFunctionCast(cfnty, self, func) (cfnty)__builtin_load_member_function_pointer(self, func)
#else
#define SimpleMemberFunctionCast(cfnty, self, func) ({ union { typeof(func) memfun; cfnty cfun; } pair; pair.memfun = func; pair.cfun; })
#endif


struct IOHIDInterface_GetElementValues_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  elementValues;
    uint32_t  count;
};
#pragma pack(4)
struct IOHIDInterface_GetElementValues_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t elementValues__descriptor;
    IOHIDInterface_GetElementValues_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_GetElementValues_Msg_ObjRefs (2)

struct IOHIDInterface_GetElementValues_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_GetElementValues_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_GetElementValues_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_GetElementValues_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_GetElementValues_Msg * message;
        struct IOHIDInterface_GetElementValues_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_GetElementValues_Invocation;
struct IOHIDInterface_SetElementValues_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  elementValues;
    uint32_t  count;
};
#pragma pack(4)
struct IOHIDInterface_SetElementValues_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t elementValues__descriptor;
    IOHIDInterface_SetElementValues_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_SetElementValues_Msg_ObjRefs (2)

struct IOHIDInterface_SetElementValues_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_SetElementValues_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_SetElementValues_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_SetElementValues_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_SetElementValues_Msg * message;
        struct IOHIDInterface_SetElementValues_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_SetElementValues_Invocation;
struct IOHIDInterface_GetSupportedCookies_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
};
#pragma pack(4)
struct IOHIDInterface_GetSupportedCookies_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    IOHIDInterface_GetSupportedCookies_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_GetSupportedCookies_Msg_ObjRefs (1)

struct IOHIDInterface_GetSupportedCookies_Rpl_Content
{
    IORPCMessage __hdr;
    OSObjectRef  cookies;
};
#pragma pack(4)
struct IOHIDInterface_GetSupportedCookies_Rpl
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t cookies__descriptor;
    IOHIDInterface_GetSupportedCookies_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_GetSupportedCookies_Rpl_ObjRefs (1)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_GetSupportedCookies_Msg * message;
        struct IOHIDInterface_GetSupportedCookies_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_GetSupportedCookies_Invocation;
struct IOHIDInterface_ReportAvailable_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
    OSObjectRef  action;
    uint64_t  timestamp;
    uint32_t  reportID;
    uint32_t  reportLength;
    IOHIDReportType  type;
};
#pragma pack(4)
struct IOHIDInterface_ReportAvailable_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    mach_msg_port_descriptor_t action__descriptor;
    IOHIDInterface_ReportAvailable_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_ReportAvailable_Msg_ObjRefs (3)

struct IOHIDInterface_ReportAvailable_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_ReportAvailable_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_ReportAvailable_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_ReportAvailable_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_ReportAvailable_Msg * message;
        struct IOHIDInterface_ReportAvailable_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_ReportAvailable_Invocation;
struct IOHIDInterface_AddReportToPool_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
};
#pragma pack(4)
struct IOHIDInterface_AddReportToPool_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    IOHIDInterface_AddReportToPool_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_AddReportToPool_Msg_ObjRefs (2)

struct IOHIDInterface_AddReportToPool_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_AddReportToPool_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_AddReportToPool_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_AddReportToPool_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_AddReportToPool_Msg * message;
        struct IOHIDInterface_AddReportToPool_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_AddReportToPool_Invocation;
struct IOHIDInterface_Open_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  forClient;
    OSObjectRef  action;
    IOOptionBits  options;
};
#pragma pack(4)
struct IOHIDInterface_Open_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t forClient__descriptor;
    mach_msg_port_descriptor_t action__descriptor;
    IOHIDInterface_Open_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_Open_Msg_ObjRefs (3)

struct IOHIDInterface_Open_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_Open_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_Open_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_Open_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_Open_Msg * message;
        struct IOHIDInterface_Open_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_Open_Invocation;
struct IOHIDInterface_Close_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  forClient;
    IOOptionBits  options;
};
#pragma pack(4)
struct IOHIDInterface_Close_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t forClient__descriptor;
    IOHIDInterface_Close_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_Close_Msg_ObjRefs (2)

struct IOHIDInterface_Close_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_Close_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_Close_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_Close_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_Close_Msg * message;
        struct IOHIDInterface_Close_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_Close_Invocation;
struct IOHIDInterface_SetReport_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
    IOHIDReportType  reportType;
    uint32_t  reportID;
    IOOptionBits  options;
};
#pragma pack(4)
struct IOHIDInterface_SetReport_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    IOHIDInterface_SetReport_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_SetReport_Msg_ObjRefs (2)

struct IOHIDInterface_SetReport_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_SetReport_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_SetReport_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_SetReport_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_SetReport_Msg * message;
        struct IOHIDInterface_SetReport_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_SetReport_Invocation;
struct IOHIDInterface_GetReport_Msg_Content
{
    IORPCMessage __hdr;
    OSObjectRef  __object;
    OSObjectRef  report;
    IOHIDReportType  reportType;
    uint32_t  reportID;
    IOOptionBits  options;
};
#pragma pack(4)
struct IOHIDInterface_GetReport_Msg
{
    IORPCMessageMach           mach;
    mach_msg_port_descriptor_t __object__descriptor;
    mach_msg_port_descriptor_t report__descriptor;
    IOHIDInterface_GetReport_Msg_Content content;
};
#pragma pack()
#define IOHIDInterface_GetReport_Msg_ObjRefs (2)

struct IOHIDInterface_GetReport_Rpl_Content
{
    IORPCMessage __hdr;
};
#pragma pack(4)
struct IOHIDInterface_GetReport_Rpl
{
    IORPCMessageMach           mach;
    IOHIDInterface_GetReport_Rpl_Content content;
};
#pragma pack()
#define IOHIDInterface_GetReport_Rpl_ObjRefs (0)


typedef union
{
    const IORPC rpc;
    struct
    {
        const struct IOHIDInterface_GetReport_Msg * message;
        struct IOHIDInterface_GetReport_Rpl       * reply;
        uint32_t sendSize;
        uint32_t replySize;
    };
}
IOHIDInterface_GetReport_Invocation;
#if !KERNEL
extern OSMetaClass * gOSContainerMetaClass;
extern OSMetaClass * gOSDataMetaClass;
extern OSMetaClass * gOSNumberMetaClass;
extern OSMetaClass * gOSBooleanMetaClass;
extern OSMetaClass * gOSDictionaryMetaClass;
extern OSMetaClass * gIODispatchQueueMetaClass;
extern OSMetaClass * gOSStringMetaClass;
extern OSMetaClass * gIOUserClientMetaClass;
extern OSMetaClass * gIOMemoryMapMetaClass;
extern OSMetaClass * gOSArrayMetaClass;
#endif /* !KERNEL */

#if !KERNEL

#define IOHIDInterface_QueueNames  ""

#define IOHIDInterface_MethodNames  ""

#define IOHIDInterfaceMetaClass_MethodNames  ""

struct OSClassDescription_IOHIDInterface_t
{
    OSClassDescription base;
    uint64_t           methodOptions[2 * 0];
    uint64_t           metaMethodOptions[2 * 0];
    char               queueNames[sizeof(IOHIDInterface_QueueNames)];
    char               methodNames[sizeof(IOHIDInterface_MethodNames)];
    char               metaMethodNames[sizeof(IOHIDInterfaceMetaClass_MethodNames)];
};

const struct OSClassDescription_IOHIDInterface_t
OSClassDescription_IOHIDInterface =
{
    .base =
    {
        .descriptionSize         = sizeof(OSClassDescription_IOHIDInterface_t),
        .name                    = "IOHIDInterface",
        .superName               = "IOService",
        .flags                   = 1*kOSClassCanRemote,
        .methodOptionsSize       = 2 * sizeof(uint64_t) * 0,
        .metaMethodOptionsSize   = 2 * sizeof(uint64_t) * 0,
        .queueNamesSize       = sizeof(IOHIDInterface_QueueNames),
        .methodNamesSize         = sizeof(IOHIDInterface_MethodNames),
        .metaMethodNamesSize     = sizeof(IOHIDInterfaceMetaClass_MethodNames),
        .methodOptionsOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDInterface_t, methodOptions),
        .metaMethodOptionsOffset = __builtin_offsetof(struct OSClassDescription_IOHIDInterface_t, metaMethodOptions),
        .queueNamesOffset     = __builtin_offsetof(struct OSClassDescription_IOHIDInterface_t, queueNames),
        .methodNamesOffset       = __builtin_offsetof(struct OSClassDescription_IOHIDInterface_t, methodNames),
        .metaMethodNamesOffset   = __builtin_offsetof(struct OSClassDescription_IOHIDInterface_t, metaMethodNames),
    },
    .methodOptions =
    {
    },
    .metaMethodOptions =
    {
    },
    .queueNames      = IOHIDInterface_QueueNames,
    .methodNames     = IOHIDInterface_MethodNames,
    .metaMethodNames = IOHIDInterfaceMetaClass_MethodNames,
};

OSMetaClass * gIOHIDInterfaceMetaClass;

static kern_return_t
IOHIDInterface_New(OSMetaClass * instance);

const OSClassLoadInformation
IOHIDInterface_Class = 
{
    .version           = 1,
    .description       = &OSClassDescription_IOHIDInterface.base,
    .instanceSize      = sizeof(IOHIDInterface),

    .metaPointer       = &gIOHIDInterfaceMetaClass,
    .New               = &IOHIDInterface_New,
};

extern const void * const
gIOHIDInterface_Declaration;
const void * const
gIOHIDInterface_Declaration
__attribute__((visibility("hidden"),section("__DATA_CONST,__osclassinfo,regular,no_dead_strip")))
    = &IOHIDInterface_Class;

static kern_return_t
IOHIDInterface_New(OSMetaClass * instance)
{
    if (!new(instance) IOHIDInterfaceMetaClass) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

kern_return_t
IOHIDInterfaceMetaClass::New(OSObject * instance)
{
    if (!new(instance) IOHIDInterface) return (kIOReturnNoMemory);
    return (kIOReturnSuccess);
}

#endif /* !KERNEL */

kern_return_t
IOHIDInterface::Dispatch(const IORPC rpc)
{
    return _Dispatch(this, rpc);
}

kern_return_t
IOHIDInterface::_Dispatch(IOHIDInterface * self, const IORPC rpc)
{
    kern_return_t ret = kIOReturnUnsupported;
    IORPCMessage * msg = IORPCMessageFromMach(rpc.message, false);

    switch (msg->msgid)
    {
#if KERNEL
        case IOHIDInterface_GetElementValues_ID:
        {
            ret = IOHIDInterface::GetElementValues_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::GetElementValues_Handler, *self, &IOHIDInterface::GetElementValues_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_SetElementValues_ID:
        {
            ret = IOHIDInterface::SetElementValues_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::SetElementValues_Handler, *self, &IOHIDInterface::SetElementValues_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_GetSupportedCookies_ID:
        {
            ret = IOHIDInterface::GetSupportedCookies_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::GetSupportedCookies_Handler, *self, &IOHIDInterface::GetSupportedCookies_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_AddReportToPool_ID:
        {
            ret = IOHIDInterface::AddReportToPool_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::AddReportToPool_Handler, *self, &IOHIDInterface::AddReportToPool_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_Open_ID:
        {
            ret = IOHIDInterface::Open_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::Open_Handler, *self, &IOHIDInterface::Open_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_Close_ID:
        {
            ret = IOHIDInterface::Close_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::Close_Handler, *self, &IOHIDInterface::Close_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_SetReport_ID:
        {
            ret = IOHIDInterface::SetReport_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::SetReport_Handler, *self, &IOHIDInterface::SetReport_Impl));
            break;
        }
#endif /* !KERNEL */
#if KERNEL
        case IOHIDInterface_GetReport_ID:
        {
            ret = IOHIDInterface::GetReport_Invoke(rpc, self, SimpleMemberFunctionCast(IOHIDInterface::GetReport_Handler, *self, &IOHIDInterface::GetReport_Impl));
            break;
        }
#endif /* !KERNEL */

        default:
            ret = IOService::_Dispatch(self, rpc);
            break;
    }

    return (ret);
}

#if KERNEL
kern_return_t
IOHIDInterface::MetaClass::Dispatch(const IORPC rpc)
{
#else /* KERNEL */
kern_return_t
IOHIDInterfaceMetaClass::Dispatch(const IORPC rpc)
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
IOHIDInterface::GetElementValues(
        uint32_t count,
        IOMemoryDescriptor * elementValues,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_GetElementValues_Msg msg;
        struct
        {
            IOHIDInterface_GetElementValues_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_GetElementValues_Msg * msg = &buf.msg;
    struct IOHIDInterface_GetElementValues_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_GetElementValues_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_GetElementValues_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_GetElementValues_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.count = count;

    msg->elementValues__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.elementValues = (OSObjectRef) elementValues;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_GetElementValues_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_GetElementValues_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::SetElementValues(
        uint32_t count,
        IOMemoryDescriptor * elementValues,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_SetElementValues_Msg msg;
        struct
        {
            IOHIDInterface_SetElementValues_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_SetElementValues_Msg * msg = &buf.msg;
    struct IOHIDInterface_SetElementValues_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_SetElementValues_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_SetElementValues_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_SetElementValues_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.count = count;

    msg->elementValues__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.elementValues = (OSObjectRef) elementValues;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_SetElementValues_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_SetElementValues_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::GetSupportedCookies(
        IOBufferMemoryDescriptor ** cookies,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_GetSupportedCookies_Msg msg;
        struct
        {
            IOHIDInterface_GetSupportedCookies_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_GetSupportedCookies_Msg * msg = &buf.msg;
    struct IOHIDInterface_GetSupportedCookies_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_GetSupportedCookies_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 0*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_GetSupportedCookies_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_GetSupportedCookies_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 1;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_GetSupportedCookies_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 1) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_GetSupportedCookies_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
        *cookies = OSDynamicCast(IOBufferMemoryDescriptor, (OSObject *) rpl->content.cookies);
        if (rpl->content.cookies && !*cookies) ret = kIOReturnBadArgument;
    }


    return (ret);
}

void
IOHIDInterface::ReportAvailable(
        uint64_t timestamp,
        uint32_t reportID,
        uint32_t reportLength,
        IOHIDReportType type,
        IOMemoryDescriptor * report,
        OSAction * action,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_ReportAvailable_Msg msg;
    } buf;
    struct IOHIDInterface_ReportAvailable_Msg * msg = &buf.msg;

    memset(msg, 0, sizeof(struct IOHIDInterface_ReportAvailable_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 1*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_ReportAvailable_ID;
    msg->content.__object = (OSObjectRef) action;
    msg->content.__hdr.objectRefs = IOHIDInterface_ReportAvailable_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 3;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->content.timestamp = timestamp;

    msg->content.reportID = reportID;

    msg->content.reportLength = reportLength;

    msg->content.type = type;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    msg->action__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.action = (OSObjectRef) action;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(*msg), .reply = NULL, .replySize = 0 };
    ret = action->Invoke(rpc);

}

kern_return_t
IOHIDInterface::AddReportToPool(
        IOBufferMemoryDescriptor * report,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_AddReportToPool_Msg msg;
        struct
        {
            IOHIDInterface_AddReportToPool_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_AddReportToPool_Msg * msg = &buf.msg;
    struct IOHIDInterface_AddReportToPool_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_AddReportToPool_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_AddReportToPool_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_AddReportToPool_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_AddReportToPool_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_AddReportToPool_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::Open(
        IOService * forClient,
        IOOptionBits options,
        OSAction * action,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_Open_Msg msg;
        struct
        {
            IOHIDInterface_Open_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_Open_Msg * msg = &buf.msg;
    struct IOHIDInterface_Open_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_Open_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_Open_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_Open_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 3;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->forClient__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.forClient = (OSObjectRef) forClient;

    msg->content.options = options;

    msg->action__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.action = (OSObjectRef) action;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_Open_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_Open_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::Close(
        IOService * forClient,
        IOOptionBits options,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_Close_Msg msg;
        struct
        {
            IOHIDInterface_Close_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_Close_Msg * msg = &buf.msg;
    struct IOHIDInterface_Close_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_Close_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_Close_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_Close_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->forClient__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.forClient = (OSObjectRef) forClient;

    msg->content.options = options;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_Close_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_Close_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::SetReport(
        IOMemoryDescriptor * report,
        IOHIDReportType reportType,
        uint32_t reportID,
        IOOptionBits options,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_SetReport_Msg msg;
        struct
        {
            IOHIDInterface_SetReport_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_SetReport_Msg * msg = &buf.msg;
    struct IOHIDInterface_SetReport_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_SetReport_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_SetReport_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_SetReport_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    msg->content.reportType = reportType;

    msg->content.reportID = reportID;

    msg->content.options = options;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_SetReport_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_SetReport_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::GetReport(
        IOMemoryDescriptor * report,
        IOHIDReportType reportType,
        uint32_t reportID,
        IOOptionBits options,
        OSDispatchMethod supermethod)
{
    kern_return_t ret;
    union
    {
        IOHIDInterface_GetReport_Msg msg;
        struct
        {
            IOHIDInterface_GetReport_Rpl rpl;
            mach_msg_max_trailer_t trailer;
        } rpl;
    } buf;
    struct IOHIDInterface_GetReport_Msg * msg = &buf.msg;
    struct IOHIDInterface_GetReport_Rpl * rpl = &buf.rpl.rpl;

    memset(msg, 0, sizeof(struct IOHIDInterface_GetReport_Msg));
    msg->mach.msgh.msgh_id   = kIORPCVersion190615;
    msg->mach.msgh.msgh_size = sizeof(*msg);
    msg->content.__hdr.flags = 0*kIORPCMessageOneway
                             | 1*kIORPCMessageSimpleReply
                             | 0*kIORPCMessageLocalHost
                             | 0*kIORPCMessageOnqueue;
    msg->content.__hdr.msgid = IOHIDInterface_GetReport_ID;
    msg->content.__object = (OSObjectRef) this;
    msg->content.__hdr.objectRefs = IOHIDInterface_GetReport_Msg_ObjRefs;
    msg->mach.msgh_body.msgh_descriptor_count = 2;

    msg->__object__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    msg->report__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;
    msg->content.report = (OSObjectRef) report;

    msg->content.reportType = reportType;

    msg->content.reportID = reportID;

    msg->content.options = options;

    IORPC rpc = { .message = &buf.msg.mach, .sendSize = sizeof(buf.msg), .reply = &buf.rpl.rpl.mach, .replySize = sizeof(buf.rpl) };
    if (supermethod) ret = supermethod((OSObject *)this, rpc);
    else             ret = ((OSObject *)this)->Invoke(rpc);

    if (kIOReturnSuccess == ret)
    do {
        {
            if (rpl->mach.msgh.msgh_size                  != sizeof(*rpl)) { ret = kIOReturnIPCError; break; };
            if (rpl->content.__hdr.msgid                  != IOHIDInterface_GetReport_ID) { ret = kIOReturnIPCError; break; };
            if (rpl->mach.msgh_body.msgh_descriptor_count != 0) { ret = kIOReturnIPCError; break; };
            if (IOHIDInterface_GetReport_Rpl_ObjRefs   != rpl->content.__hdr.objectRefs) { ret = kIOReturnIPCError; break; };
        }
    }
    while (false);
    if (kIOReturnSuccess == ret)
    {
    }


    return (ret);
}

kern_return_t
IOHIDInterface::GetElementValues_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        GetElementValues_Handler func)
{
    IOHIDInterface_GetElementValues_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOMemoryDescriptor * elementValues;

    if (IOHIDInterface_GetElementValues_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    elementValues = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.elementValues);
    if (!elementValues && rpc.message->content.elementValues) return (kIOReturnBadArgument);

    ret = (*func)(target,
        rpc.message->content.count,
        elementValues);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_GetElementValues_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_GetElementValues_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::SetElementValues_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        SetElementValues_Handler func)
{
    IOHIDInterface_SetElementValues_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOMemoryDescriptor * elementValues;

    if (IOHIDInterface_SetElementValues_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    elementValues = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.elementValues);
    if (!elementValues && rpc.message->content.elementValues) return (kIOReturnBadArgument);

    ret = (*func)(target,
        rpc.message->content.count,
        elementValues);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_SetElementValues_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_SetElementValues_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::GetSupportedCookies_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        GetSupportedCookies_Handler func)
{
    IOHIDInterface_GetSupportedCookies_Invocation rpc = { _rpc };
    kern_return_t ret;

    if (IOHIDInterface_GetSupportedCookies_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);

    ret = (*func)(target,
        (IOBufferMemoryDescriptor **)&rpc.reply->content.cookies);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_GetSupportedCookies_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 1;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_GetSupportedCookies_Rpl_ObjRefs;
    rpc.reply->cookies__descriptor.type = MACH_MSG_PORT_DESCRIPTOR;

    return (ret);
}

kern_return_t
IOHIDInterface::ReportAvailable_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        ReportAvailable_Handler func)
{
    return IOHIDInterface::ReportAvailable_Invoke(_rpc, target, func, NULL);
}

kern_return_t
IOHIDInterface::ReportAvailable_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        ReportAvailable_Handler func,
        const OSMetaClass * targetActionClass)
{
    IOHIDInterface_ReportAvailable_Invocation rpc = { _rpc };
    IOMemoryDescriptor * report;
    OSAction * action;

    if (IOHIDInterface_ReportAvailable_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);
    if (targetActionClass) {
        action = (OSAction *) OSMetaClassBase::safeMetaCast((OSObject *) rpc.message->content.action, targetActionClass);
    } else {
        action = OSDynamicCast(OSAction, (OSObject *) rpc.message->content.action);
    }
    if (!action && rpc.message->content.action) return (kIOReturnBadArgument);

    (*func)(target,
        rpc.message->content.timestamp,
        rpc.message->content.reportID,
        rpc.message->content.reportLength,
        rpc.message->content.type,
        report,
        action);


    return (kIOReturnSuccess);
}

kern_return_t
IOHIDInterface::AddReportToPool_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        AddReportToPool_Handler func)
{
    IOHIDInterface_AddReportToPool_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOBufferMemoryDescriptor * report;

    if (IOHIDInterface_AddReportToPool_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOBufferMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);

    ret = (*func)(target,
        report);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_AddReportToPool_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_AddReportToPool_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::Open_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        Open_Handler func)
{
    IOHIDInterface_Open_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOService * forClient;
    OSAction * action;

    if (IOHIDInterface_Open_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    forClient = OSDynamicCast(IOService, (OSObject *) rpc.message->content.forClient);
    if (!forClient && rpc.message->content.forClient) return (kIOReturnBadArgument);
    action = OSDynamicCast(OSAction, (OSObject *) rpc.message->content.action);
    if (!action && rpc.message->content.action) return (kIOReturnBadArgument);

    ret = (*func)(target,
        forClient,
        rpc.message->content.options,
        action);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_Open_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_Open_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::Close_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        Close_Handler func)
{
    IOHIDInterface_Close_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOService * forClient;

    if (IOHIDInterface_Close_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    forClient = OSDynamicCast(IOService, (OSObject *) rpc.message->content.forClient);
    if (!forClient && rpc.message->content.forClient) return (kIOReturnBadArgument);

    ret = (*func)(target,
        forClient,
        rpc.message->content.options);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_Close_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_Close_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::SetReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        SetReport_Handler func)
{
    IOHIDInterface_SetReport_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOMemoryDescriptor * report;

    if (IOHIDInterface_SetReport_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);

    ret = (*func)(target,
        report,
        rpc.message->content.reportType,
        rpc.message->content.reportID,
        rpc.message->content.options);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_SetReport_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_SetReport_Rpl_ObjRefs;

    return (ret);
}

kern_return_t
IOHIDInterface::GetReport_Invoke(const IORPC _rpc,
        OSMetaClassBase * target,
        GetReport_Handler func)
{
    IOHIDInterface_GetReport_Invocation rpc = { _rpc };
    kern_return_t ret;
    IOMemoryDescriptor * report;

    if (IOHIDInterface_GetReport_Msg_ObjRefs != rpc.message->content.__hdr.objectRefs) return (kIOReturnIPCError);
    report = OSDynamicCast(IOMemoryDescriptor, (OSObject *) rpc.message->content.report);
    if (!report && rpc.message->content.report) return (kIOReturnBadArgument);

    ret = (*func)(target,
        report,
        rpc.message->content.reportType,
        rpc.message->content.reportID,
        rpc.message->content.options);

    if (kIOReturnSuccess != ret) return (ret);

    rpc.reply->content.__hdr.msgid = IOHIDInterface_GetReport_ID;
    rpc.reply->content.__hdr.flags = kIORPCMessageOneway;
    rpc.reply->mach.msgh.msgh_id   = kIORPCVersion190615Reply;
    rpc.reply->mach.msgh.msgh_size = sizeof(*rpc.reply);
    rpc.reply->mach.msgh_body.msgh_descriptor_count = 0;
    rpc.reply->content.__hdr.objectRefs = IOHIDInterface_GetReport_Rpl_ObjRefs;

    return (ret);
}



