/*
 * kernel/power/incremental.c
 *
 * Copyright (C) 2012 Nigel Cunningham (nigel at tuxonice net)
 *
 * This file is released under the GPLv2.
 *
 * This file contains routines related to storing incremental images - that
 * is, retaining an image after an initial cycle and then storing incremental
 * changes on subsequent hibernations.
 */

#include <linux/suspend.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/crypto.h>

#include "tuxonice_builtin.h"
#include "tuxonice.h"
#include "tuxonice_modules.h"
#include "tuxonice_sysfs.h"
#include "tuxonice_io.h"
#include "tuxonice_ui.h"
#include "tuxonice_alloc.h"

static struct toi_module_ops toi_incremental_ops;
static struct toi_module_ops *next_driver;
static unsigned long toi_incremental_bytes_in, toi_incremental_bytes_out;

static char toi_incremental_slow_cmp_name[32] = "sha1";

static DEFINE_MUTEX(stats_lock);

struct cpu_context {
	u8 *page_buffer;
	struct crypto_hash *transform;
	unsigned int len;
	u8 *buffer_start;
	u8 *output_buffer;
};

#define OUT_BUF_SIZE (2 * PAGE_SIZE)

static DEFINE_PER_CPU(struct cpu_context, contexts);

#if 0
/*
 * toi_fletcher16
 *
 * Calculate the fletcher16 checksum of a page
 */
static uint16_t toi_incremental_fletcher16(char *data)
{
	int sum1 = 0xffff, sum2 = 0xffff;
	int len = PAGE_SIZE;
	while (len)
	{
		int tlen = len > 360 ? 360 : len;
		len -= tlen;
		do
		{
			sum1 += *data++;
			sum2 += sum1;
		} while ((--tlen) != 0);
		sum1 = (sum1 & 0xffff) + (sum1 >> 16);
		sum2 = (sum2 & 0xffff) + (sum2 >> 16);
	}
	sum1 = (sum1 & 0xffff) + (sum1 >> 16);
	sum2 = (sum2 & 0xffff) + (sum2 >> 16);
	return sum2 << 16 | sum1;
}
#endif

/*
 * toi_crypto_prepare
 *
 * Prepare to do some work by allocating buffers and transforms.
 */
static int toi_incremental_crypto_prepare(void)
{
	int cpu;

	if (!*toi_incremental_slow_cmp_name) {
		printk(KERN_INFO "TuxOnIce: Compression enabled but no "
				"compressor name set.\n");
		return 1;
	}

	for_each_online_cpu(cpu) {
		struct cpu_context *this = &per_cpu(contexts, cpu);
		this->transform = crypto_alloc_hash(toi_incremental_slow_cmp_name, 0, 0);
		if (IS_ERR(this->transform)) {
			printk(KERN_INFO "TuxOnIce: Failed to initialise the "
					"%s hashing transform.\n",
					toi_incremental_slow_cmp_name);
			this->transform = NULL;
			return 1;
		}

		this->page_buffer =
			(char *) toi_get_zeroed_page(16, TOI_ATOMIC_GFP);

		if (!this->page_buffer) {
			printk(KERN_ERR
			  "Failed to allocate a page buffer for TuxOnIce "
			  "hashing driver.\n");
			return -ENOMEM;
		}

		this->output_buffer =
			(char *) vmalloc_32(OUT_BUF_SIZE);

		if (!this->output_buffer) {
			printk(KERN_ERR
			  "Failed to allocate a output buffer for TuxOnIce "
			  "hashing driver.\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static int toi_incremental_rw_cleanup(int writing)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct cpu_context *this = &per_cpu(contexts, cpu);
		if (this->transform) {
			crypto_free_hash(this->transform);
			this->transform = NULL;
		}

		if (this->page_buffer)
			toi_free_page(16, (unsigned long) this->page_buffer);

		this->page_buffer = NULL;

		if (this->output_buffer)
			vfree(this->output_buffer);

		this->output_buffer = NULL;
	}

	return 0;
}

/*
 * toi_incremental_init
 */

static int toi_incremental_init(int hibernate_or_resume)
{
	if (!hibernate_or_resume)
		return 0;

	next_driver = toi_get_next_filter(&toi_incremental_ops);

	return next_driver ? 0 : -ECHILD;
}

/*
 * toi_incremental_rw_init()
 */

static int toi_incremental_rw_init(int rw, int stream_number)
{
	if (toi_incremental_crypto_prepare()) {
		printk(KERN_ERR "Failed to initialise hashing "
				"algorithm.\n");
		if (rw == READ) {
			printk(KERN_INFO "Unable to read the image.\n");
			return -ENODEV;
		} else {
			printk(KERN_INFO "Continuing without "
				"compressing the image.\n");
			toi_incremental_ops.enabled = 0;
		}
	}

	return 0;
}

/*
 * toi_incremental_write_page()
 *
 * Compress a page of data, buffering output and passing on filled
 * pages to the next module in the pipeline.
 *
 * Buffer_page:	Pointer to a buffer of size PAGE_SIZE, containing
 * data to be compressed.
 *
 * Returns:	0 on success. Otherwise the error is that returned by later
 * 		modules, -ECHILD if we have a broken pipeline or -EIO if
 * 		zlib errs.
 */
static int toi_incremental_write_page(unsigned long index, int buf_type,
		void *buffer_page, unsigned int buf_size)
{
	int ret = 0, cpu = smp_processor_id();
	struct cpu_context *ctx = &per_cpu(contexts, cpu);
	u8* output_buffer = buffer_page;
	int output_len = buf_size;
	int out_buf_type = buf_type;

	if (ctx->transform) {

		ctx->buffer_start = TOI_MAP(buf_type, buffer_page);
		ctx->len = OUT_BUF_SIZE;
/*
		ret = crypto_comp_compress(ctx->transform,
			ctx->buffer_start, buf_size,
			ctx->output_buffer, &ctx->len);
*/
		TOI_UNMAP(buf_type, buffer_page);

		toi_message(TOI_COMPRESS, TOI_VERBOSE, 0,
				"CPU %d, index %lu: %d bytes",
				cpu, index, ctx->len);

		if (!ret && ctx->len < buf_size) { /* some compression */
			output_buffer = ctx->output_buffer;
			output_len = ctx->len;
			out_buf_type = TOI_VIRT;
		}

	}

	mutex_lock(&stats_lock);

	toi_incremental_bytes_in += buf_size;
	toi_incremental_bytes_out += output_len;

	mutex_unlock(&stats_lock);

	if (!ret)
		ret = next_driver->write_page(index, out_buf_type,
				output_buffer, output_len);

	return ret;
}

/*
 * toi_incremental_read_page()
 * @buffer_page: struct page *. Pointer to a buffer of size PAGE_SIZE.
 *
 * Retrieve data from later modules and decompress it until the input buffer
 * is filled.
 * Zero if successful. Error condition from me or from downstream on failure.
 */
static int toi_incremental_read_page(unsigned long *index, int buf_type,
		void *buffer_page, unsigned int *buf_size)
{
	int ret, cpu = smp_processor_id();
	unsigned int len;
	unsigned int outlen = PAGE_SIZE;
	char *buffer_start;
	struct cpu_context *ctx = &per_cpu(contexts, cpu);

	if (!ctx->transform)
		return next_driver->read_page(index, TOI_PAGE, buffer_page,
				buf_size);

	/*
	 * All our reads must be synchronous - we can't decompress
	 * data that hasn't been read yet.
	 */

	ret = next_driver->read_page(index, TOI_VIRT, ctx->page_buffer, &len);

	buffer_start = kmap(buffer_page);

	/* Error or uncompressed data */
	if (ret || len == PAGE_SIZE) {
		memcpy(buffer_start, ctx->page_buffer, len);
		goto out;
	}
/*
	ret = crypto_comp_decompress(
			ctx->transform,
			ctx->page_buffer,
			len, buffer_start, &outlen);
*/
	toi_message(TOI_COMPRESS, TOI_VERBOSE, 0,
			"CPU %d, index %lu: %d=>%d (%d).",
			cpu, *index, len, outlen, ret);

	if (ret)
		abort_hibernate(TOI_FAILED_IO,
			"Compress_read returned %d.\n", ret);
	else if (outlen != PAGE_SIZE) {
		abort_hibernate(TOI_FAILED_IO,
			"Decompression yielded %d bytes instead of %ld.\n",
			outlen, PAGE_SIZE);
		printk(KERN_ERR "Decompression yielded %d bytes instead of "
				"%ld.\n", outlen, PAGE_SIZE);
		ret = -EIO;
		*buf_size = outlen;
	}
out:
	TOI_UNMAP(buf_type, buffer_page);
	return ret;
}

/*
 * toi_incremental_print_debug_stats
 * @buffer: Pointer to a buffer into which the debug info will be printed.
 * @size: Size of the buffer.
 *
 * Print information to be recorded for debugging purposes into a buffer.
 * Returns: Number of characters written to the buffer.
 */

static int toi_incremental_print_debug_stats(char *buffer, int size)
{
	unsigned long pages_in = toi_incremental_bytes_in >> PAGE_SHIFT,
		      pages_out = toi_incremental_bytes_out >> PAGE_SHIFT;
	int len;

	/* Output the compression ratio achieved. */
	if (*toi_incremental_slow_cmp_name)
		len = scnprintf(buffer, size, "- Compressor is '%s'.\n",
				toi_incremental_slow_cmp_name);
	else
		len = scnprintf(buffer, size, "- Compressor is not set.\n");

	if (pages_in)
		len += scnprintf(buffer+len, size - len, "  Compressed "
			"%lu bytes into %lu (%ld percent compression).\n",
		  toi_incremental_bytes_in,
		  toi_incremental_bytes_out,
		  (pages_in - pages_out) * 100 / pages_in);
	return len;
}

/*
 * toi_incremental_compression_memory_needed
 *
 * Tell the caller how much memory we need to operate during hibernate/resume.
 * Returns: Unsigned long. Maximum number of bytes of memory required for
 * operation.
 */
static int toi_incremental_memory_needed(void)
{
	return 2 * PAGE_SIZE;
}

static int toi_incremental_storage_needed(void)
{
	return 2 * sizeof(unsigned long) + sizeof(int) +
		strlen(toi_incremental_slow_cmp_name) + 1;
}

/*
 * toi_incremental_save_config_info
 * @buffer: Pointer to a buffer of size PAGE_SIZE.
 *
 * Save informaton needed when reloading the image at resume time.
 * Returns: Number of bytes used for saving our data.
 */
static int toi_incremental_save_config_info(char *buffer)
{
	int len = strlen(toi_incremental_slow_cmp_name) + 1, offset = 0;

	*((unsigned long *) buffer) = toi_incremental_bytes_in;
	offset += sizeof(unsigned long);
	*((unsigned long *) (buffer + offset)) = toi_incremental_bytes_out;
	offset += sizeof(unsigned long);
	*((int *) (buffer + offset)) = len;
	offset += sizeof(int);
	strncpy(buffer + offset, toi_incremental_slow_cmp_name, len);
	return offset + len;
}

/* toi_incremental_load_config_info
 * @buffer: Pointer to the start of the data.
 * @size: Number of bytes that were saved.
 *
 * Description:	Reload information needed for decompressing the image at
 * resume time.
 */
static void toi_incremental_load_config_info(char *buffer, int size)
{
	int len, offset = 0;

	toi_incremental_bytes_in = *((unsigned long *) buffer);
	offset += sizeof(unsigned long);
	toi_incremental_bytes_out = *((unsigned long *) (buffer + offset));
	offset += sizeof(unsigned long);
	len = *((int *) (buffer + offset));
	offset += sizeof(int);
	strncpy(toi_incremental_slow_cmp_name, buffer + offset, len);
}

static void toi_incremental_pre_atomic_restore(struct toi_boot_kernel_data *bkd)
{
	bkd->compress_bytes_in = toi_incremental_bytes_in;
	bkd->compress_bytes_out = toi_incremental_bytes_out;
}

static void toi_incremental_post_atomic_restore(struct toi_boot_kernel_data *bkd)
{
	toi_incremental_bytes_in = bkd->compress_bytes_in;
	toi_incremental_bytes_out = bkd->compress_bytes_out;
}

/*
 * data for our sysfs entries.
 */
static struct toi_sysfs_data sysfs_params[] = {
	SYSFS_INT("enabled", SYSFS_RW, &toi_incremental_ops.enabled, 0, 1, 0,
			NULL),
	SYSFS_STRING("algorithm", SYSFS_RW, toi_incremental_slow_cmp_name, 31, 0, NULL),
};

/*
 * Ops structure.
 */
static struct toi_module_ops toi_incremental_ops = {
	.type			= FILTER_MODULE,
	.name			= "compression",
	.directory		= "compression",
	.module			= THIS_MODULE,
	.initialise		= toi_incremental_init,
	.memory_needed 		= toi_incremental_memory_needed,
	.print_debug_info	= toi_incremental_print_debug_stats,
	.save_config_info	= toi_incremental_save_config_info,
	.load_config_info	= toi_incremental_load_config_info,
	.storage_needed		= toi_incremental_storage_needed,

	.pre_atomic_restore	= toi_incremental_pre_atomic_restore,
	.post_atomic_restore	= toi_incremental_post_atomic_restore,

	.rw_init		= toi_incremental_rw_init,
	.rw_cleanup		= toi_incremental_rw_cleanup,

	.write_page		= toi_incremental_write_page,
	.read_page		= toi_incremental_read_page,

	.sysfs_data		= sysfs_params,
	.num_sysfs_entries	= sizeof(sysfs_params) /
		sizeof(struct toi_sysfs_data),
};

/* ---- Registration ---- */

static __init int toi_incremental_load(void)
{
	return toi_register_module(&toi_incremental_ops);
}

#ifdef MODULE
static __exit void toi_incremental_unload(void)
{
	toi_unregister_module(&toi_incremental_ops);
}

module_init(toi_incremental_load);
module_exit(toi_incremental_unload);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nigel Cunningham");
MODULE_DESCRIPTION("Incremental Image Support for TuxOnIce");
#else
late_initcall(toi_incremental_load);
#endif
