#ifndef __EROFS_FS_H
#define __EROFS_FS_H

#define EROFS_SUPER_OFFSET      1024

#define EROFS_FEATURE_INCOMPAT_LZ4_0PADDING	0x00000001
#define EROFS_ALL_FEATURE_INCOMPAT		EROFS_FEATURE_INCOMPAT_LZ4_0PADDING

struct erofs_super_block {
	__le32 magic;
	__le32 checksum;
	__le32 feature_compat;
	__u8 blkszbits;
	__u8 reserved;

	__le16 root_nid;
	__le64 inos;

	__le64 build_time;
	__le32 build_time_nsec;
	__le32 blocks;
	__le32 meta_blkaddr;
	__le32 xattr_blkaddr;
	__u8 uuid[16];
	__u8 volume_name[16];
	__le32 feature_incompat;
	union {
		__le16 available_compr_algs;
		__le16 lz4_max_distance;
	} __packed u1;
	__u8 reserved2[42];
};

enum {
	EROFS_INODE_FLAT_PLAIN			= 0,
	EROFS_INODE_FLAT_COMPRESSION_LEGACY	= 1,
	EROFS_INODE_FLAT_INLINE			= 2,
	EROFS_INODE_FLAT_COMPRESSION		= 3,
	EROFS_INODE_DATALAYOUT_MAX
};

static inline bool erofs_inode_is_data_compressed(unsigned int datamode)
{
	return datamode == EROFS_INODE_FLAT_COMPRESSION ||
		datamode == EROFS_INODE_FLAT_COMPRESSION_LEGACY;
}

#define EROFS_I_VERSION_BITS            1
#define EROFS_I_DATALAYOUT_BITS         3

#define EROFS_I_VERSION_BIT             0
#define EROFS_I_DATALAYOUT_BIT          1

#define EROFS_I_ALL	\
	((1 << (EROFS_I_DATALAYOUT_BIT + EROFS_I_DATALAYOUT_BITS)) - 1)

struct erofs_inode_compact {
	__le16 i_format;

	__le16 i_xattr_icount;
	__le16 i_mode;
	__le16 i_nlink;
	__le32 i_size;
	__le32 i_reserved;
	union {

		__le32 compressed_blocks;
		__le32 raw_blkaddr;

		__le32 rdev;
	} i_u;
	__le32 i_ino;
	__le16 i_uid;
	__le16 i_gid;
	__le32 i_reserved2;
};

#define EROFS_INODE_LAYOUT_COMPACT	0

#define EROFS_INODE_LAYOUT_EXTENDED	1

struct erofs_inode_extended {
	__le16 i_format;

	__le16 i_xattr_icount;
	__le16 i_mode;
	__le16 i_reserved;
	__le64 i_size;
	union {

		__le32 compressed_blocks;
		__le32 raw_blkaddr;

		__le32 rdev;
	} i_u;

	__le32 i_ino;

	__le32 i_uid;
	__le32 i_gid;
	__le64 i_ctime;
	__le32 i_ctime_nsec;
	__le32 i_nlink;
	__u8   i_reserved2[16];
};

#define EROFS_MAX_SHARED_XATTRS         (128)

#define EROFS_SHARED_XATTR_EXTENT       (255)

struct erofs_xattr_ibody_header {
	__le32 h_reserved;
	__u8   h_shared_count;
	__u8   h_reserved2[7];
	__le32 h_shared_xattrs[0];
};

#define EROFS_XATTR_INDEX_USER              1
#define EROFS_XATTR_INDEX_POSIX_ACL_ACCESS  2
#define EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT 3
#define EROFS_XATTR_INDEX_TRUSTED           4
#define EROFS_XATTR_INDEX_LUSTRE            5
#define EROFS_XATTR_INDEX_SECURITY          6

struct erofs_xattr_entry {
	__u8   e_name_len;
	__u8   e_name_index;
	__le16 e_value_size;

	char   e_name[0];
};

static inline unsigned int erofs_xattr_ibody_size(__le16 i_xattr_icount)
{
	if (!i_xattr_icount)
		return 0;

	return sizeof(struct erofs_xattr_ibody_header) +
		sizeof(__u32) * (le16_to_cpu(i_xattr_icount) - 1);
}

#define EROFS_XATTR_ALIGN(size) round_up(size, sizeof(struct erofs_xattr_entry))

static inline unsigned int erofs_xattr_entry_size(struct erofs_xattr_entry *e)
{
	return EROFS_XATTR_ALIGN(sizeof(struct erofs_xattr_entry) +
				 e->e_name_len + le16_to_cpu(e->e_value_size));
}

enum {
	Z_EROFS_COMPRESSION_LZ4	= 0,
	Z_EROFS_COMPRESSION_MAX
};

#define Z_EROFS_ADVISE_COMPACTED_2B_BIT         0

#define Z_EROFS_ADVISE_COMPACTED_2B     (1 << Z_EROFS_ADVISE_COMPACTED_2B_BIT)

struct z_erofs_map_header {
	__le32	h_reserved1;
	__le16	h_advise;

	__u8	h_algorithmtype;

	__u8	h_clusterbits;
};

#define Z_EROFS_VLE_LEGACY_HEADER_PADDING       8

enum {
	Z_EROFS_VLE_CLUSTER_TYPE_PLAIN		= 0,
	Z_EROFS_VLE_CLUSTER_TYPE_HEAD		= 1,
	Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD	= 2,
	Z_EROFS_VLE_CLUSTER_TYPE_RESERVED	= 3,
	Z_EROFS_VLE_CLUSTER_TYPE_MAX
};

#define Z_EROFS_VLE_DI_CLUSTER_TYPE_BITS        2
#define Z_EROFS_VLE_DI_CLUSTER_TYPE_BIT         0

struct z_erofs_vle_decompressed_index {
	__le16 di_advise;

	__le16 di_clusterofs;

	union {

		__le32 blkaddr;

		__le16 delta[2];
	} di_u;
};

#define Z_EROFS_VLE_LEGACY_INDEX_ALIGN(size) \
	(round_up(size, sizeof(struct z_erofs_vle_decompressed_index)) + \
	 sizeof(struct z_erofs_map_header) + Z_EROFS_VLE_LEGACY_HEADER_PADDING)

struct erofs_dirent {
	__le64 nid;
	__le16 nameoff;
	__u8 file_type;
	__u8 reserved;
} __packed;

#define EROFS_NAME_LEN      255

static inline void erofs_check_ondisk_layout_definitions(void)
{
	BUILD_BUG_ON(sizeof(struct erofs_super_block) != 128);
	BUILD_BUG_ON(sizeof(struct erofs_inode_compact) != 32);
	BUILD_BUG_ON(sizeof(struct erofs_inode_extended) != 64);
	BUILD_BUG_ON(sizeof(struct erofs_xattr_ibody_header) != 12);
	BUILD_BUG_ON(sizeof(struct erofs_xattr_entry) != 4);
	BUILD_BUG_ON(sizeof(struct z_erofs_map_header) != 8);
	BUILD_BUG_ON(sizeof(struct z_erofs_vle_decompressed_index) != 8);
	BUILD_BUG_ON(sizeof(struct erofs_dirent) != 12);

	BUILD_BUG_ON(BIT(Z_EROFS_VLE_DI_CLUSTER_TYPE_BITS) <
		     Z_EROFS_VLE_CLUSTER_TYPE_MAX - 1);
}

#endif
