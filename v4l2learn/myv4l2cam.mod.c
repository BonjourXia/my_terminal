#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x8d42f378, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x2d7643a9, __VMLINUX_SYMBOL_STR(param_ops_int) },
	{ 0xe8df11d0, __VMLINUX_SYMBOL_STR(vb2_ioctl_streamoff) },
	{ 0xb85a06ba, __VMLINUX_SYMBOL_STR(vb2_ioctl_streamon) },
	{ 0x938bc63e, __VMLINUX_SYMBOL_STR(vb2_ioctl_prepare_buf) },
	{ 0x5c9cf758, __VMLINUX_SYMBOL_STR(vb2_ioctl_create_bufs) },
	{ 0x5c605fb0, __VMLINUX_SYMBOL_STR(vb2_ioctl_dqbuf) },
	{ 0x8c291cd1, __VMLINUX_SYMBOL_STR(vb2_ioctl_expbuf) },
	{ 0xdaf3dac7, __VMLINUX_SYMBOL_STR(vb2_ioctl_qbuf) },
	{ 0xb0de04d0, __VMLINUX_SYMBOL_STR(vb2_ioctl_querybuf) },
	{ 0x33265739, __VMLINUX_SYMBOL_STR(vb2_ioctl_reqbufs) },
	{ 0xb3cc923c, __VMLINUX_SYMBOL_STR(vb2_fop_release) },
	{ 0x7370c25b, __VMLINUX_SYMBOL_STR(v4l2_fh_open) },
	{ 0xf84a7d17, __VMLINUX_SYMBOL_STR(vb2_fop_mmap) },
	{ 0x6b083a59, __VMLINUX_SYMBOL_STR(video_ioctl2) },
	{ 0x93b46c0, __VMLINUX_SYMBOL_STR(vb2_fop_poll) },
	{ 0x7b65bf00, __VMLINUX_SYMBOL_STR(vb2_fop_read) },
	{ 0x71110d0c, __VMLINUX_SYMBOL_STR(vb2_queue_release) },
	{ 0x558a76a0, __VMLINUX_SYMBOL_STR(video_unregister_device) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0x57adbfc3, __VMLINUX_SYMBOL_STR(v4l2_device_unregister) },
	{ 0xb8c3d444, __VMLINUX_SYMBOL_STR(__video_register_device) },
	{ 0xf1607f8, __VMLINUX_SYMBOL_STR(video_device_release) },
	{ 0x1201148a, __VMLINUX_SYMBOL_STR(video_device_alloc) },
	{ 0xe7010993, __VMLINUX_SYMBOL_STR(vb2_queue_init) },
	{ 0x1fb3766c, __VMLINUX_SYMBOL_STR(vb2_vmalloc_memops) },
	{ 0x704a994a, __VMLINUX_SYMBOL_STR(v4l2_device_register) },
	{ 0x5ee52022, __VMLINUX_SYMBOL_STR(init_timer_key) },
	{ 0xd67bd86a, __VMLINUX_SYMBOL_STR(__mutex_init) },
	{ 0x1bec770f, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0x5363d73f, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xc87c1f84, __VMLINUX_SYMBOL_STR(ktime_get) },
	{ 0xaf645445, __VMLINUX_SYMBOL_STR(vb2_plane_vaddr) },
	{ 0xa38caae0, __VMLINUX_SYMBOL_STR(mod_timer) },
	{ 0x7d11c268, __VMLINUX_SYMBOL_STR(jiffies) },
	{ 0x7f02188f, __VMLINUX_SYMBOL_STR(__msecs_to_jiffies) },
	{ 0x692d6370, __VMLINUX_SYMBOL_STR(vb2_buffer_done) },
	{ 0xfc982daa, __VMLINUX_SYMBOL_STR(del_timer_sync) },
	{ 0xfa2a45e, __VMLINUX_SYMBOL_STR(__memzero) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0xe707d823, __VMLINUX_SYMBOL_STR(__aeabi_uidiv) },
	{ 0x87bc08ad, __VMLINUX_SYMBOL_STR(video_devdata) },
	{ 0x73e20c1c, __VMLINUX_SYMBOL_STR(strlcpy) },
	{ 0x51d559d1, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x598542b2, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0x2e5810c6, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr1) },
	{ 0xb1ad28e0, __VMLINUX_SYMBOL_STR(__gnu_mcount_nc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "636003BD638AFFDD3E0FC17");
