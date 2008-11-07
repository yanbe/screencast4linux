#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{ 0xe189b8a7, "struct_module" },
	{ 0x92dc1c64, "video_device_release" },
	{ 0x60a4461c, "__up_wakeup" },
	{ 0x17ade7ad, "remap_pfn_range" },
	{ 0xcb51d0fd, "vmalloc_to_pfn" },
	{ 0x96b27088, "__down_failed" },
	{ 0xda4008e6, "cond_resched" },
	{ 0xcd7da3a9, "video_register_device" },
	{ 0xffd3c7, "init_waitqueue_head" },
	{ 0x1d26aa98, "sprintf" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x2e60bace, "memcpy" },
	{ 0x61c4ca72, "video_device_alloc" },
	{ 0xd5976294, "kmem_cache_alloc" },
	{ 0xcba6c63f, "kmalloc_caches" },
	{ 0x2f287f0d, "copy_to_user" },
	{ 0xd6c963c, "copy_from_user" },
	{ 0x932da67e, "kill_proc" },
	{ 0x37a0cba, "kfree" },
	{ 0x2fd1d81c, "vfree" },
	{ 0x7cc50746, "video_unregister_device" },
	{ 0x1b7d4074, "printk" },
	{ 0x8dc513b8, "per_cpu__current_task" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=videodev";


MODULE_INFO(srcversion, "9C6BAD28A7DC80D9617E272");
