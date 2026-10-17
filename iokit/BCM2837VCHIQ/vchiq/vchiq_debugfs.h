/*
 * Linux's vchiq_debugfs.h, reduced to what vchiq_arm.h's instance struct
 * names: this port has no debugfs (the core's state dump is not exported).
 */
#ifndef VCHIQ_DEBUGFS_H
#define VCHIQ_DEBUGFS_H

struct vchiq_debugfs_node {
	int unused;
};

#define vchiq_debugfs_init()                    ((void)0)
#define vchiq_debugfs_deinit()                  ((void)0)
#define vchiq_debugfs_add_instance(instance)    ((void)(instance))
#define vchiq_debugfs_remove_instance(instance) ((void)(instance))

#endif /* VCHIQ_DEBUGFS_H */
