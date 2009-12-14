#include <linux/blkdev.h>
#include <linux/ctype.h>

#if 0
#define PRINTK(fmt, args...) printk(KERN_DEBUG fmt, ## args)
#define PRINT_HEX_DUMP(v1, v2, v3, v4, v5, v6, v7, v8) \
	print_hex_dump(v1, v2, v3, v4, v5, v6, v7, v8)
#else
#define PRINTK(fmt, args...)
#define PRINT_HEX_DUMP(v1, v2, v3, v4, v5, v6, v7, v8)
#endif

/*
 * Simple UUID translation
 */

struct uuid_info {
	const char *key;
	const char *name;
	long bkoff;
	unsigned sboff;
	unsigned sig_len;
	const char *magic;
	int uuid_offset;
};

/*
 * Based on libuuid's blkid_magic array. Note that I don't
 * have uuid offsets for all of these yet - mssing ones are 0x0.
 * Further information welcome.
 *
 * Rearranged by page of fs signature for optimisation.
 */
static struct uuid_info uuid_list[] = {
  { NULL,	"oracleasm",	0,	32,  8, "ORCLDISK",		0x0 },
  { "ntfs",	"ntfs",		0,	 3,  8, "NTFS    ",		0x0 },
  { "vfat",	"vfat",		0,   0x52,  5, "MSWIN",                0x0 },
  { "vfat", 	"vfat",		0,   0x52,  8, "FAT32   ",             0x0 },
  { "vfat",	"vfat",		0,   0x36,  5, "MSDOS",                0x0 },
  { "vfat",	"vfat",		0,   0x36,  8, "FAT16   ",             0x0 },
  { "vfat",	"vfat",		0,   0x36,  8, "FAT12   ",             0x0 },
  { "vfat",	"vfat",		0,      0,  1, "\353",                 0x0 },
  { "vfat",	"vfat",		0,      0,  1, "\351",                 0x0 },
  { "vfat",	"vfat",		0,  0x1fe,  2, "\125\252",             0x0 },
  { "xfs",	"xfs",		0,	 0,  4, "XFSB",			0x14 },
  { "romfs",	"romfs",	0,	 0,  8, "-rom1fs-",		0x0 },
  { "bfs",	"bfs",		0,	 0,  4, "\316\372\173\033",	0 },
  { "cramfs",	"cramfs",	0,	 0,  4, "E=\315\050",		0x0 },
  { "qnx4",	"qnx4",		0,	 4,  6, "QNX4FS",		0 },
  { NULL,	"crypt_LUKS",	0,	 0,  6,	"LUKS\xba\xbe",		0x0 },
  { "squashfs",	"squashfs",	0,	 0,  4,	"sqsh",			0 },
  { "squashfs",	"squashfs",	0,	 0,  4,	"hsqs",			0 },
  { "ocfs",	"ocfs",		0,	 8,  9,	"OracleCFS",		0x0 },
  { "lvm2pv",	"lvm2pv",	0,  0x018,  8, "LVM2 001",		0x0 },
  { "sysv",	"sysv",		0,  0x3f8,  4, "\020~\030\375",	0 },
  { "jbd",	"jbd",		1,   0x38,  2, "\123\357",		0x0 },
  { "ext",	"ext4dev",	1,   0x38,  2, "\123\357",		0x468 },
  { "ext",	"ext4",	 	1,   0x38,  2, "\123\357",		0x468 },
  { "ext",	"ext3",	 	1,   0x38,  2, "\123\357",		0x468 },
  { "ext",	"ext2",		1,   0x38,  2, "\123\357",		0x468 },
  { "minix",	"minix",	1,   0x10,  2, "\177\023",             0 },
  { "minix",	"minix",	1,   0x10,  2, "\217\023",             0 },
  { "minix",	"minix",	1,   0x10,  2, "\150\044",		0 },
  { "minix",	"minix",	1,   0x10,  2, "\170\044",		0 },
  { "lvm2pv",	"lvm2pv",	1,  0x018,  8, "LVM2 001",		0x0 },
  { "vxfs",	"vxfs",		1,	 0,  4, "\365\374\001\245",	0 },
  { "hfsplus",	"hfsplus",	1,	 0,  2, "BD",			0x0 },
  { "hfsplus",	"hfsplus",	1,	 0,  2, "H+",			0x0 },
  { "hfsplus",	"hfsplus",	1,	 0,  2, "HX",			0x0 },
  { "hfs",	"hfs",	 	1,	 0,  2, "BD",			0x0 },
  { "ocfs2",	"ocfs2",	1,	 0,  6,	"OCFSV2",		0x0 },
  { "lvm2pv",	"lvm2pv",	0,  0x218,  8, "LVM2 001",		0x0 },
  { "lvm2pv",	"lvm2pv",	1,  0x218,  8, "LVM2 001",		0x0 },
  { "ocfs2",	"ocfs2",	2,	 0,  6,	"OCFSV2",		0x0 },
  { "swap",	"swap",		0,  0xff6, 10, "SWAP-SPACE",		0x40c },
  { "swap",	"swap",		0,  0xff6, 10, "SWAPSPACE2",		0x40c },
  { "swap",	"swsuspend",	0,  0xff6,  9, "S1SUSPEND",		0x40c },
  { "swap",	"swsuspend",	0,  0xff6,  9, "S2SUSPEND",		0x40c },
  { "swap",	"swsuspend",	0,  0xff6,  9, "ULSUSPEND",		0x40c },
  { "ocfs2",	"ocfs2",	4,	 0,  6,	"OCFSV2",		0x0 },
  { "ocfs2",	"ocfs2",	8,	 0,  6,	"OCFSV2",		0x0 },
  { "hpfs",	"hpfs",		8,	0,  4, "I\350\225\371",	0 },
  { "reiserfs",	"reiserfs",	8,   0x34,  8, "ReIsErFs",		0x10054 },
  { "reiserfs",	"reiserfs",	8,	20,  8, "ReIsErFs",		0x10054 },
  { "zfs",	"zfs",		8,	 0,  8, "\0\0\x02\xf5\xb0\x07\xb1\x0c", 0x0 },
  { "zfs",	"zfs",		8,	 0,  8, "\x0c\xb1\x07\xb0\xf5\x02\0\0", 0x0 },
  { "ufs",	"ufs",	 	8,  0x55c,  4, "T\031\001\000",	0 },
  { "swap",	"swap",	 	0, 0x1ff6, 10, "SWAP-SPACE",		0x40c },
  { "swap",	"swap",	 	0, 0x1ff6, 10, "SWAPSPACE2",		0x40c },
  { "swap",	"swsuspend",	0, 0x1ff6,  9, "S1SUSPEND",		0x40c },
  { "swap",	"swsuspend",	0, 0x1ff6,  9, "S2SUSPEND",		0x40c },
  { "swap",	"swsuspend",	0, 0x1ff6,  9, "ULSUSPEND",		0x40c },
  { "reiserfs",	"reiserfs",	64,   0x34,  9, "ReIsEr2Fs",		0x10054 },
  { "reiserfs",	"reiserfs",	64,   0x34,  9, "ReIsEr3Fs",		0x10054 },
  { "reiserfs",	"reiserfs",	64,   0x34,  8, "ReIsErFs",		0x10054 },
  { "reiser4",	"reiser4",	64,	 0,  7, "ReIsEr4",		0x100544 },
  { "gfs2",	"gfs2",		64,      0,  4, "\x01\x16\x19\x70",     0x0 },
  { "gfs",	"gfs",		64,	0,  4, "\x01\x16\x19\x70",     0x0 },
  { "btrfs",	"btrfs",	64,   0x40,  8, "_BHRfS_M",		0x0 },
  { "swap",	"swap",	 	0, 0x3ff6, 10, "SWAP-SPACE",		0x40c },
  { "swap",	"swap",	 	0, 0x3ff6, 10, "SWAPSPACE2",		0x40c },
  { "swap",	"swsuspend", 	0, 0x3ff6,  9, "S1SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0,	0x3ff6,  9, "S2SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0, 0x3ff6,  9, "ULSUSPEND",		0x40c },
  { "udf",	"udf",		32,	 1,  5, "BEA01",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "BOOT2",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "CD001",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "CDW02",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "NSR02",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "NSR03",		0x0 },
  { "udf",	"udf",		32,	 1,  5, "TEA01",		0x0 },
  { "iso9660",	"iso9660",	32,	 1,  5, "CD001",		0x0 },
  { "iso9660",	"iso9660",	32,	 9,  5, "CDROM",		0x0 },
  { "jfs",	"jfs",		32,	 0,  4, "JFS1",			0x88 },
  { "swap",	"swap",	 	0, 0x7ff6, 10, "SWAP-SPACE",		0x40c },
  { "swap",	"swap",	 	0, 0x7ff6, 10, "SWAPSPACE2",		0x40c },
  { "swap",	"swsuspend", 	0, 0x7ff6,  9, "S1SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0, 0x7ff6,  9, "S2SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0, 0x7ff6,  9, "ULSUSPEND",		0x40c },
  { "swap",	"swap",	 	0, 0xfff6, 10, "SWAP-SPACE",		0x40c },
  { "swap",	"swap",	 	0, 0xfff6, 10, "SWAPSPACE2",		0x40c },
  { "swap",	"swsuspend", 	0, 0xfff6,  9, "S1SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0, 0xfff6,  9, "S2SUSPEND",		0x40c },
  { "swap",	"swsuspend", 	0, 0xfff6,  9, "ULSUSPEND",		0x40c },
  { "zfs",	"zfs",		264,	 0,  8, "\0\0\x02\xf5\xb0\x07\xb1\x0c", 0x0 },
  { "zfs",	"zfs",		264,	 0,  8, "\x0c\xb1\x07\xb0\xf5\x02\0\0", 0x0 },
  { NULL,	NULL,		0,      0,  0, NULL,			0x0 }
};

static void uuid_end_bio(struct bio *bio, int err)
{
	struct page *page = bio->bi_io_vec[0].bv_page;

	BUG_ON(!test_bit(BIO_UPTODATE, &bio->bi_flags));

	unlock_page(page);
	bio_put(bio);
}


/**
 * submit - submit BIO request
 * @dev: The block device we're using.
 * @page_num: The page we're reading.
 *
 * Based on Patrick Mochell's pmdisk code from long ago: "Straight from the
 * textbook - allocate and initialize the bio. If we're writing, make sure
 * the page is marked as dirty. Then submit it and carry on."
 **/
static struct page *read_bdev_page(struct block_device *dev, int page_num)
{
	struct bio *bio = NULL;
	struct page *page = alloc_page(GFP_KERNEL);

	if (!page)
		return NULL;

	lock_page(page);

	bio = bio_alloc(GFP_KERNEL, 1);
	bio->bi_bdev = dev;
	bio->bi_sector = page_num << 3;
	bio->bi_end_io = uuid_end_bio;

	PRINTK("Submitting bio on page %lx, page %d.\n",
			(unsigned long) dev->bd_dev, page_num);

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		printk(KERN_DEBUG "ERROR: adding page to bio at %d\n",
				page_num);
		bio_put(bio);
		__free_page(page);
		printk(KERN_DEBUG "read_bdev_page freed page %p (in error "
				"path).\n", page);
		return ERR_PTR(-EFAULT);
	}

	submit_bio(READ | (1 << BIO_RW_SYNCIO) |
			(1 << BIO_RW_UNPLUG), bio);

	wait_on_page_bit(page, PG_locked);
	return page;
}

int bdev_matches_key(struct block_device *bdev, const char *key)
{
	unsigned char *data = NULL;
	struct page *data_page = NULL;

	int dev_offset, pg_num, pg_off, i;
	int last_pg_num = -1;
	int result = 0;
	char buf[50];

	if (!bdev->bd_disk) {
		bdevname(bdev, buf);
		PRINTK("bdev %s has no bd_disk.\n", buf);
		return 0;
	}

	if (!bdev->bd_disk->queue) {
		bdevname(bdev, buf);
		PRINTK("bdev %s has no queue.\n", buf);
		return 0;
	}

	for (i = 0; uuid_list[i].name; i++) {
		struct uuid_info *dat = &uuid_list[i];

		if (!dat->key || strcmp(dat->key, key))
			continue;

		dev_offset = (dat->bkoff << 10) + dat->sboff;
		pg_num = dev_offset >> 12;
		pg_off = dev_offset & 0xfff;

		if ((((pg_num + 1) << 3) - 1) > bdev->bd_part->nr_sects >> 1)
			continue;

		if (pg_num != last_pg_num) {
			if (data_page)
				__free_page(data_page);
			data_page = read_bdev_page(bdev, pg_num);
			data = page_address(data_page);
		}

		last_pg_num = pg_num;

		if (strncmp(&data[pg_off], dat->magic, dat->sig_len))
			continue;

		result = 1;
		break;
	}

	if (data_page)
		__free_page(data_page);

	return result;
}

int part_matches_uuid(struct hd_struct *part, const char *uuid)
{
	struct block_device *bdev;
	unsigned char *data = NULL;
	struct page *data_page = NULL;

	int dev_offset, pg_num, pg_off;
	int uuid_pg_num, uuid_pg_off, i;
	unsigned char *uuid_data = NULL;
	struct page *uuid_data_page = NULL;

	int last_pg_num = -1, last_uuid_pg_num = 0;
	int result = 0;
	char buf[50];

	bdev = bdget(part_devt(part));

	PRINTK("blkdev_get %p.\n", part);

	if (blkdev_get(bdev, FMODE_READ)) {
		PRINTK("blkdev_get failed.\n");
		return 0;
	}

	if (!bdev->bd_disk) {
		bdevname(bdev, buf);
		PRINTK("bdev %s has no bd_disk.\n", buf);
		goto out;
	}

	if (!bdev->bd_disk->queue) {
		bdevname(bdev, buf);
		PRINTK("bdev %s has no queue.\n", buf);
		goto out;
	}

	PRINT_HEX_DUMP(KERN_EMERG, "part_matches_uuid looking for ",
			DUMP_PREFIX_NONE, 16, 1, uuid, 16, 0);

	for (i = 0; uuid_list[i].name; i++) {
		struct uuid_info *dat = &uuid_list[i];
		dev_offset = (dat->bkoff << 10) + dat->sboff;
		pg_num = dev_offset >> 12;
		pg_off = dev_offset & 0xfff;
		uuid_pg_num = dat->uuid_offset >> 12;
		uuid_pg_off = dat->uuid_offset & 0xfff;

		if ((((pg_num + 1) << 3) - 1) > part->nr_sects >> 1)
			continue;

		if (pg_num != last_pg_num) {
			if (data_page)
				__free_page(data_page);
			data_page = read_bdev_page(bdev, pg_num);
			data = page_address(data_page);
		}

		last_pg_num = pg_num;

		if (strncmp(&data[pg_off], dat->magic, dat->sig_len))
			continue;

		/* Does the UUID match? */
		if (uuid_pg_num > part->nr_sects >> 3)
			continue;

		if (!uuid_data || uuid_pg_num != last_uuid_pg_num) {
			if (uuid_data_page)
				__free_page(uuid_data_page);
			uuid_data_page = read_bdev_page(bdev, uuid_pg_num);
			uuid_data = page_address(uuid_data_page);
		}

		last_uuid_pg_num = uuid_pg_num;

		PRINT_HEX_DUMP(KERN_EMERG, "part_matches_uuid considering ",
				DUMP_PREFIX_NONE, 16, 1,
				&uuid_data[uuid_pg_off], 16, 0);

		if (!memcmp(&uuid_data[uuid_pg_off], uuid, 16)) {
			PRINTK("We have a match.\n");
			result = 1;
			break;
		}
	}

	if (data_page)
		__free_page(data_page);

	if (uuid_data_page)
		__free_page(uuid_data_page);

out:
	blkdev_put(bdev, FMODE_READ);
	return result;
}

int uuid_from_block_dev(struct block_device *bdev, char *uuid)
{
	unsigned char *data = NULL;
	struct page *data_page = NULL;

	int dev_offset, pg_num, pg_off;
	int uuid_pg_num, uuid_pg_off, i;
	unsigned char *uuid_data = NULL;
	struct page *uuid_data_page = NULL;

	int last_pg_num = -1, last_uuid_pg_num = 0;
	int result = 1;
	char buf[50];

	bdevname(bdev, buf);

	PRINTK(KERN_EMERG "uuid_from_block_dev looking for partition type "
			"of %s.\n", buf);

	for (i = 0; uuid_list[i].name; i++) {
		struct uuid_info *dat = &uuid_list[i];
		dev_offset = (dat->bkoff << 10) + dat->sboff;
		pg_num = dev_offset >> 12;
		pg_off = dev_offset & 0xfff;
		uuid_pg_num = dat->uuid_offset >> 12;
		uuid_pg_off = dat->uuid_offset & 0xfff;

		if ((((pg_num + 1) << 3) - 1) > bdev->bd_part->nr_sects >> 1)
			continue;

		if (pg_num != last_pg_num) {
			if (data_page)
				__free_page(data_page);
			data_page = read_bdev_page(bdev, pg_num);
			data = page_address(data_page);
		}

		last_pg_num = pg_num;

		if (strncmp(&data[pg_off], dat->magic, dat->sig_len))
			continue;

		/* UUID can't be off the end of the disk */
		if (uuid_pg_num > bdev->bd_part->nr_sects >> 3)
			continue;

		PRINTK("This partition looks like %s.\n", dat->name);

		if (!uuid_data || uuid_pg_num != last_uuid_pg_num) {
			if (uuid_data_page)
				__free_page(uuid_data_page);
			uuid_data_page = read_bdev_page(bdev, uuid_pg_num);
			uuid_data = page_address(uuid_data_page);
		}

		last_uuid_pg_num = uuid_pg_num;

		if (!uuid_pg_off) {
			PRINTK("Don't know uuid offset for %s. Continuing the "
					"search.\n", dat->name);
		} else {
			memcpy(uuid, &uuid_data[uuid_pg_off], 16);
			PRINT_HEX_DUMP(KERN_EMERG, "uuid_from_block_dev "
					"returning ", DUMP_PREFIX_NONE, 16, 1,
					uuid, 16, 0);
			result = 0;
			break;
		}
	}

	if (data_page)
		__free_page(data_page);

	if (uuid_data_page)
		__free_page(uuid_data_page);

	return result;
}
EXPORT_SYMBOL_GPL(uuid_from_block_dev);
