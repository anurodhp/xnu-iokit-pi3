/* iig(DriverKit-99) generated from OSAction.iig */

/* OSAction.iig:1-36 */
/*
 * Copyright (c) 2019-2019 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#ifndef _IOKIT_OSACTION_H
#define _IOKIT_OSACTION_H

#include <DriverKit/OSObject.h>  /* .iig include */

typedef void (^OSActionCancelHandler)(void);
typedef void (^OSActionAbortedHandler)(void);

/* source class OSAction OSAction.iig:37-109 */

#if __DOCUMENTATION__
#define KERNEL IIG_KERNEL

/*!
 * @class OSAction
 *
 * @abstract
 * OSAction is an object that represents a callback to be be invoked.
 *
 * @discussion
 * The callback is specified as a method and object pair.
 * State associated with the callback may be allocated and stored for the creator of the object.
 * Methods to allocate an OSAction instance are generated for each method defined in a class with
 * a TYPE attribute, so there should not be any need to directly call OSAction::Create().
 */

class NATIVE KERNEL OSAction : public OSObject
{
public:

    /*!
     * @brief       Create an instance of OSAction.
	 * @discussion  Methods to allocate an OSAction instance are generated for each method defined in a class with
     *              a TYPE attribute, so there should not be any need to directly call OSAction::Create().
     * @param       target OSObject to receive the callback. This object will be retained until the OSAction is
     *              canceled or freed.
     * @param       targetmsgid Generated message ID for the target method.
     * @param       msgid Generated message ID for the method invoked by the receiver of the OSAction
     *              to generate the callback.
     * @param       referenceSize Size of additional state structure available to the creator of the OSAction
     *              with GetReference.
     * @param       action Created OSAction with +1 retain count to be released by the caller.
     * @return      kIOReturnSuccess on success. See IOReturn.h for error codes.
     */
	static kern_return_t
	Create(
		OSObject      * target,
		uint64_t        targetmsgid,
		uint64_t        msgid,
		size_t          referenceSize,
		OSAction     ** action) LOCAL;

	virtual void
	free() override;

    /*!
     * @brief       Return a pointer to any state allocated by the OSAction creator.
     * @discussion  Reference data is allocated with zero initialized content. It may be set and retrieved later
     *              with this method.
     * @return      A pointer to storage for the owner. It will be NULL if referenceSize was zero, and NULL
     *              when called in a process other than the owner that is receiving the OSAction as a parameter.
     */
	void *
	GetReference() LOCALONLY;

    /*!
     * @brief       Cancel all callbacks from the action.
     * @discussion  After cancellation, the action can only be freed. It cannot be reactivated.
     * @param       handler Handler block to be invoked after any callbacks have completed.
     * @return      kIOReturnSuccess on success. See IOReturn.h for error codes.
     */
	kern_return_t
	Cancel(OSActionCancelHandler handler) LOCALONLY;

    /*!
     * @brief       Install a handler to be invoked when no other processes reference the action.
     * @discussion  When all tasks other than the creator release their references to the action,
     *              invoke the handler in the owner. A task exiting will always remove its references.
     * @param       handler Handler block to be invoked on no more references.
     * @return      kIOReturnSuccess on success. See IOReturn.h for error codes.
     */
	kern_return_t
	SetAbortedHandler(OSActionAbortedHandler handler) LOCALONLY;

	virtual void
	Aborted(void) LOCAL;
};

#undef KERNEL
#else /* __DOCUMENTATION__ */

/* generated class OSAction OSAction.iig:37-109 */

#define OSAction_Create_ID            0xaa1fc3ce85ce5497ULL
#define OSAction_Aborted_ID            0xbfb95094c657d68fULL

#define OSAction_Create_Args \
        OSObject * target, \
        uint64_t targetmsgid, \
        uint64_t msgid, \
        size_t referenceSize, \
        OSAction ** action

#define OSAction_Aborted_Args \


#define OSAction_Methods \
\
public:\
\
    virtual kern_return_t\
    Dispatch(const IORPC rpc) APPLE_KEXT_OVERRIDE;\
\
    static kern_return_t\
    _Dispatch(OSAction * self, const IORPC rpc);\
\
    static kern_return_t\
    Create(\
        OSObject * target,\
        uint64_t targetmsgid,\
        uint64_t msgid,\
        size_t referenceSize,\
        OSAction ** action);\
\
    void *\
    GetReference(\
);\
\
    kern_return_t\
    Cancel(\
        OSActionCancelHandler handler);\
\
    kern_return_t\
    SetAbortedHandler(\
        OSActionAbortedHandler handler);\
\
    void\
    Aborted(\
        OSDispatchMethod supermethod = NULL);\
\
\
protected:\
    /* _Impl methods */\
\
    static kern_return_t\
    Create_Call(OSAction_Create_Args);\
\
    void\
    Aborted_Impl(OSAction_Aborted_Args);\
\
\
public:\
    /* _Invoke methods */\
\
    typedef kern_return_t (*Create_Handler)(OSAction_Create_Args);\
    static kern_return_t\
    Create_Invoke(const IORPC rpc,\
        Create_Handler func);\
\
    typedef void (*Aborted_Handler)(OSMetaClassBase * targetOSAction_Aborted_Args);\
    static kern_return_t\
    Aborted_Invoke(const IORPC rpc,\
        OSMetaClassBase * target,\
        Aborted_Handler func);\
\


#define OSAction_KernelMethods \
\
protected:\
    /* _Impl methods */\
\
    static kern_return_t\
    Create_Impl(OSAction_Create_Args);\
\


#define OSAction_VirtualMethods \
\
public:\
\
    virtual void\
    free(\
) APPLE_KEXT_OVERRIDE;\
\


#if !KERNEL

extern OSMetaClass          * gOSActionMetaClass;
extern const OSClassLoadInformation OSAction_Class;

class OSActionMetaClass : public OSMetaClass
{
public:
    virtual kern_return_t
    New(OSObject * instance) override;
    virtual kern_return_t
    Dispatch(const IORPC rpc) override;
};

#endif /* !KERNEL */

class OSActionInterface : public OSInterface
{
public:
};

struct OSAction_IVars;
struct OSAction_LocalIVars;

class OSAction : public OSObject, public OSActionInterface
{
#if KERNEL
    OSDeclareDefaultStructorsWithDispatch(OSAction);
#endif /* KERNEL */

#if !KERNEL
    friend class OSActionMetaClass;
#endif /* !KERNEL */

public:
    union
    {
        OSAction_IVars * ivars;
        OSAction_LocalIVars * lvars;
    };
#if !KERNEL
    virtual const OSMetaClass *
    getMetaClass() const APPLE_KEXT_OVERRIDE { return OSTypeID(OSAction); };
#endif /* KERNEL */

    using super = OSObject;

#if !KERNEL
    OSAction_Methods
#endif /* !KERNEL */

    OSAction_VirtualMethods
};

#endif /* !__DOCUMENTATION__ */

/* OSAction.iig:111- */

#endif /* ! _IOKIT_OSACTION_H */
