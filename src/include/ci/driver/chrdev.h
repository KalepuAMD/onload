/* SPDX-License-Identifier: GPL-2.0 */
/* X-SPDX-Copyright-Text: (c) Copyright 2019-2020 Xilinx, Inc. */
#ifndef CI_DRIVER_CHRDEV_H_
#define CI_DRIVER_CHRDEV_H_
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include "ci/driver/kernel_compat.h"

/* This file contains some simple utility functions for creating char
 * devices and their corresponding nodes in /dev, which are needed by most of
 * the drivers comprising Onload */

struct ci_chrdev_registration {
  dev_t devid;
  int count;
  struct class* class;
  struct cdev* cdevs[0];
};

struct ci_chrdev_node_params {
  const char* name;
  struct file_operations* fops;
  umode_t mode;
};


ci_inline void destroy_chrdev_and_mknod(struct ci_chrdev_registration* reg)
{
  int i;

  for( i = reg->count - 1; i >= 0; --i ) {
    dev_t devid = MKDEV(MAJOR(reg->devid), MINOR(reg->devid) + i);
    device_destroy(reg->class, devid);
    if( reg->cdevs[i] )
      cdev_del(reg->cdevs[i]);
  }
  if( reg->class )
    class_destroy(reg->class);
  if( reg->devid )
    unregister_chrdev_region(reg->devid, reg->count);
  kfree(reg);
}


#ifdef EFRM_CLASS_DEVNODE_DEV_IS_CONST
/* Linux >= 6.2 */
ci_inline char* chrdev_devnode_set_mode(const struct device* dev,
                                        umode_t* mode)
#else
ci_inline char* chrdev_devnode_set_mode(struct device* dev,
                                        umode_t* mode)
#endif
{
  if( mode )
    *mode = (umode_t)(uintptr_t)dev_get_drvdata(dev);
  return NULL;
}


ci_inline int create_chrdev_and_mknod(int major, int minor, const char* name,
                        int count, const struct ci_chrdev_node_params* nodes,
                        struct ci_chrdev_registration** reg_out)
{
  struct ci_chrdev_registration* reg;
  int rc;
  int i;

  reg = kzalloc(offsetof(struct ci_chrdev_registration, cdevs) +
                sizeof(reg->cdevs[0]) * count, GFP_KERNEL);
  if( ! reg ) {
    printk(KERN_ERR "%s: can't allocate %s memory", __func__, name);
    return -ENOMEM;
  }

  if( major <= 0 ) {
    rc = alloc_chrdev_region(&reg->devid, minor, count, name);
  }
  else {
    reg->devid = MKDEV(major, minor);
    rc = register_chrdev_region(reg->devid, count, name);
  }
  if( rc < 0 ) {
    printk(KERN_ERR "%s: can't register %s chrdev (%d)", __func__, name, rc);
    goto fail_free;
  }

  reg->class = ci_class_create(name);
  if( IS_ERR(reg->class) ) {
    rc = PTR_ERR(reg->class);
    reg->class = NULL;
    printk(KERN_ERR "%s: can't allocate %s class (%d)", __func__, name, rc);
    goto fail_free;
  }
  reg->class->devnode = chrdev_devnode_set_mode;

  for( i = 0; i < count; ++i ) {
    dev_t devid = MKDEV(MAJOR(reg->devid), MINOR(reg->devid) + i);
    struct device* dev;

    reg->cdevs[i] = cdev_alloc();
    if( reg->cdevs[i] == NULL ) {
      printk(KERN_ERR "%s: can't alloc %s char device",
             __func__, nodes[i].name);
      goto fail_free;
    }
    reg->cdevs[i]->owner = THIS_MODULE;
    reg->cdevs[i]->ops = nodes[i].fops;
    rc = cdev_add(reg->cdevs[i], devid, 1);
    if( rc < 0 ) {
      printk(KERN_ERR "%s: can't add %s char device (%d)",
             __func__, nodes[i].name, rc);
      goto fail_free;
    }

    /* We only need to remember 16 bits per device, so cast the actual value
     * of 'mode' to a pointer rather than using an extra level of indirection
     * and more kallocation. The casting here matches the uncasting in
     * chrdev_devnode_set_mode */
    dev = device_create(reg->class, NULL, devid,
                        (void*)(uintptr_t)nodes[i].mode,
                        "%s", nodes[i].name);
    if( IS_ERR(dev) ) {
      rc = PTR_ERR(dev);
      printk(KERN_ERR "%s: can't allocate %s device (%d)",
             __func__, nodes[i].name, rc);
      cdev_del(reg->cdevs[i]);
      goto fail_free;
    }
    reg->count = i + 1;
  }

  *reg_out = reg;
  return 0;

 fail_free:
  destroy_chrdev_and_mknod(reg);
  return rc;
}


ci_inline int create_one_chrdev_and_mknod(int major, const char* name,
                                      struct file_operations* fops,
                                      struct ci_chrdev_registration** reg_out)
{
  struct ci_chrdev_node_params node = {
    .name = name,
    .fops = fops,
    .mode = 0666,
  };
  return create_chrdev_and_mknod(major, 0, name, 1, &node, reg_out);
}

#endif
