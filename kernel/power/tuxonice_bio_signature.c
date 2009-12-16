/*
 * kernel/power/tuxonice_bio_signature.c
 *
 * Copyright (C) 2004-2008 Nigel Cunningham (nigel at tuxonice net)
 *
 * Distributed under GPLv2.
 *
 */

#include <linux/uuid.h>

#include "tuxonice.h"
#include "tuxonice_sysfs.h"
#include "tuxonice_modules.h"
#include "tuxonice_prepare_image.h"
#include "tuxonice_bio.h"
#include "tuxonice_ui.h"
#include "tuxonice_alloc.h"
#include "tuxonice_io.h"
#include "tuxonice_builtin.h"
#include "tuxonice_bio_internal.h"

struct sig_data *toi_sig_data;

/* Struct of swap header pages */

union diskpage {
	union swap_header swh;	/* swh.magic is the only member used */
	struct sig_data sig_data;
};

union p_diskpage {
	union diskpage *pointer;
	char *ptr;
	unsigned long address;
};

char *toi_cur_sig_page;
char *toi_orig_sig_page;
int have_image;

int get_signature_page(void)
{
	if (!toi_cur_sig_page) {
		toi_message(TOI_IO, TOI_VERBOSE, 0,
				"Allocating current signature page.");
		toi_cur_sig_page = (char *) toi_get_zeroed_page(38,
			TOI_ATOMIC_GFP);
		if (!toi_cur_sig_page) {
			printk(KERN_ERR "Failed to allocate memory for the "
				"current image signature.\n");
			return -ENOMEM;
		}

		toi_sig_data = (struct sig_data *) toi_cur_sig_page;
	}

	toi_message(TOI_IO, TOI_VERBOSE, 0, "Reading signature from dev %lx,"
			" sector %d.",
			resume_block_device->bd_dev, resume_firstblock);

	return toi_bio_ops.bdev_page_io(READ, resume_block_device,
		resume_firstblock, virt_to_page(toi_cur_sig_page));
}

void forget_signature_page(void)
{
	if (toi_cur_sig_page) {
		toi_sig_data = NULL;
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Freeing toi_cur_sig_page"
				" (%p).", toi_cur_sig_page);
		toi_free_page(38, (unsigned long) toi_cur_sig_page);
		toi_cur_sig_page = NULL;
	}

	if (toi_orig_sig_page) {
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Freeing toi_orig_sig_page"
				" (%p).", toi_orig_sig_page);
		toi_free_page(38, (unsigned long) toi_orig_sig_page);
		toi_orig_sig_page = NULL;
	}
}

/*
 * We need to ensure we use the signature page that's currently on disk,
 * so as to not remove the image header. Post-atomic-restore, the orig sig
 * page will be empty, so we can use that as our method of knowing that we
 * need to load the on-disk signature and not use the non-image sig in
 * memory. (We're going to powerdown after writing the change, so it's safe.
 */
int toi_bio_mark_resume_attempted(int flag)
{
	toi_message(TOI_IO, TOI_VERBOSE, 0, "Make resume attempted = %d.",
			flag);
	if (!toi_orig_sig_page) {
		forget_signature_page();
		get_signature_page();
	}
	toi_sig_data->resumed_before = flag;
	return toi_bio_ops.bdev_page_io(WRITE, resume_block_device,
		resume_firstblock, virt_to_page(toi_cur_sig_page));
}

int toi_bio_mark_have_image(void)
{
	int result;
	char buf[32];

	toi_message(TOI_IO, TOI_VERBOSE, 0, "Recording that an image exists.");
	memcpy(toi_sig_data->sig, tuxonice_signature,
			sizeof(tuxonice_signature));
	toi_sig_data->have_image = 1;
	toi_sig_data->resumed_before = 0;
	toi_sig_data->header_dev_t = get_header_dev_t();
	toi_sig_data->have_uuid = 0;

	result = uuid_from_block_dev(get_header_bdev(),
			toi_sig_data->header_uuid);
	if (!result) {
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Got uuid for dev_t %s.",
				format_dev_t(buf, get_header_dev_t()));
		toi_sig_data->have_uuid = 1;
	} else
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Could not get uuid for "
				"dev_t %s.",
				format_dev_t(buf, get_header_dev_t()));

	toi_sig_data->first_header_block = get_headerblock();
	have_image = 1;
	toi_message(TOI_IO, TOI_VERBOSE, 0, "header dev_t is %x. First block "
			"is %d.", toi_sig_data->header_dev_t,
			toi_sig_data->first_header_block);

	return toi_bio_ops.bdev_page_io(WRITE, resume_block_device,
		resume_firstblock, virt_to_page(toi_cur_sig_page));
}

/*
 * toi_bio_restore_original_signature - restore the original signature
 *
 * At boot time (aborting pre atomic-restore), toi_orig_sig_page gets used.
 * It will have the original signature page contents, stored in the image
 * header. Post atomic-restore, we use :toi_cur_sig_page, which will contain
 * the contents that were loaded when we started the cycle.
 */
int toi_bio_restore_original_signature(void)
{
	char *use = toi_orig_sig_page ? toi_orig_sig_page : toi_cur_sig_page;

	if (!use) {
		printk("toi_bio_restore_original_signature: No signature "
				"page loaded.\n");
		return 0;
	}

	toi_message(TOI_IO, TOI_VERBOSE, 0, "Recording that no image exists.");
	have_image = 0;
	toi_sig_data->have_image = 0;
	return toi_bio_ops.bdev_page_io(WRITE, resume_block_device,
		resume_firstblock, virt_to_page(use));
}

/*
 * check_for_signature - See whether we have an image.
 *
 * Returns 0 if no image, 1 if there is one, -1 if indeterminate.
 */
int toi_check_for_signature(void)
{
	union p_diskpage swap_header_page;
	int type;
	const char *normal_sigs[] = {"SWAP-SPACE", "SWAPSPACE2" };
	const char *swsusp_sigs[] = {"S1SUSP", "S2SUSP", "S1SUSPEND" };
	char *swap_header;

	if (!toi_cur_sig_page) {
		int result = get_signature_page();

		if (result)
			return result;
	}

	/*
	 * Start by looking for the binary header.
	 */
	if (!memcmp(tuxonice_signature, toi_cur_sig_page,
				sizeof(tuxonice_signature))) {
		have_image = toi_sig_data->have_image;
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Have binary signature. "
				"Have image is %d.", have_image);
		if (have_image)
			toi_message(TOI_IO, TOI_VERBOSE, 0, "header dev_t is "
					"%x. First block is %d.",
					toi_sig_data->header_dev_t,
					toi_sig_data->first_header_block);
		return toi_sig_data->have_image;
	}

	/*
	 * Failing that, try old file allocator headers.
	 */

	if (!memcmp(HaveImage, toi_cur_sig_page, strlen(HaveImage))) {
		have_image = 1;
		return 1;
	}

	have_image = 0;

	if (!memcmp(NoImage, toi_cur_sig_page, strlen(NoImage)))
		return 0;

	/*
	 * Nope? How about swap?
	 */
	swap_header_page = (union p_diskpage) toi_cur_sig_page;
	swap_header = swap_header_page.pointer->swh.magic.magic;

	/* Normal swapspace? */
	for (type = 0; type < 2; type++)
		if (!memcmp(normal_sigs[type], swap_header,
					strlen(normal_sigs[type])))
			return 0;

	/* Swsusp or uswsusp? */
	for (type = 0; type < 3; type++)
		if (!memcmp(swsusp_sigs[type], swap_header,
					strlen(swsusp_sigs[type])))
			return 2;

	return -1;
}

/*
 * Image_exists
 *
 * Returns -1 if don't know, otherwise 0 (no) or 1 (yes).
 */
int toi_bio_image_exists(int quiet)
{
	int result;
	char *orig_sig_page = toi_cur_sig_page;
	char *msg = NULL;

	toi_message(TOI_IO, TOI_VERBOSE, 0, "toi_bio_image_exists.");

	if (!resume_dev_t) {
		if (!quiet)
			printk(KERN_INFO "Not even trying to read header "
				"because resume_dev_t is not set.\n");
		return -1;
	}

	if (!resume_block_device) {
		toi_message(TOI_IO, TOI_VERBOSE, 0, "Opening resume_dev_t %lx.",
				resume_dev_t);
		resume_block_device = toi_open_bdev(NULL, resume_dev_t, 1);
		if (IS_ERR(resume_block_device)) {
			if (!quiet)
				printk(KERN_INFO "Failed to open resume dev_t"
						" (%x).\n", resume_dev_t);
			return -1;
		}
	}

	result = toi_check_for_signature();

	clear_toi_state(TOI_RESUMED_BEFORE);
	if (toi_sig_data->resumed_before)
		set_toi_state(TOI_RESUMED_BEFORE);

	if (quiet || result == -ENOMEM)
		goto out;

	if (result == -1)
		msg = "TuxOnIce: Unable to find a signature."
				" Could you have moved a swap file?\n";
	else if (!result)
		msg = "TuxOnIce: No image found.\n";
	else if (result == 1)
		msg = "TuxOnIce: Image found.\n";
	else if (result == 2)
		msg = "TuxOnIce: uswsusp or swsusp image found.\n";

	printk(KERN_INFO "%s", msg);

out:
	if (!orig_sig_page)
		forget_signature_page();

	return result;
}

int toi_bio_scan_for_image(int quiet)
{
	struct block_device *bdev;
	char default_name[255] = "";

	if (!quiet)
		printk(KERN_DEBUG "Scanning swap devices for TuxOnIce "
				"signature...\n");
	for (bdev = next_bdev_of_type(NULL, "swap"); bdev;
				bdev = next_bdev_of_type(bdev, "swap")) {
		int result;
		char name[255] = "";
		sprintf(name, "%u:%u", MAJOR(bdev->bd_dev),
				MINOR(bdev->bd_dev));
		if (!quiet)
			printk(KERN_DEBUG "- Trying %s.\n", name);
		resume_block_device = bdev;
		resume_dev_t = bdev->bd_dev;

		result = toi_check_for_signature();

		resume_block_device = NULL;
		resume_dev_t = MKDEV(0, 0);

		if (!default_name[0])
			strcpy(default_name, name);

		if (result == 1) {
			/* Got one! */
			strcpy(resume_file, name);
			next_bdev_of_type(bdev, NULL);
			if (!quiet)
				printk(KERN_DEBUG " ==> Image found on %s.\n",
						resume_file);
			return 1;
		}
		forget_signature_page();
	}

	if (!quiet)
		printk(KERN_DEBUG "TuxOnIce scan: No image found.\n");
	strcpy(resume_file, default_name);
	return 0;
}
