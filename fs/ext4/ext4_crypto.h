/*
 * linux/fs/ext4/ext4_crypto.h
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption header content for ext4
 *
 * Written by Michael Halcrow, 2015.
 */

#ifndef _EXT4_CRYPTO_H
#define _EXT4_CRYPTO_H

#include <linux/fs.h>

#define EXT4_KEY_DESCRIPTOR_SIZE 8

/* Policy provided via an ioctl on the topmost directory */
struct ext4_encryption_policy {
	char version;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
} __attribute__((__packed__));

#define EXT4_ENCRYPTION_CONTEXT_FORMAT_V1 1
#define EXT4_KEY_DERIVATION_NONCE_SIZE 16

#define EXT4_POLICY_FLAGS_PAD_4		0x00
#define EXT4_POLICY_FLAGS_PAD_8		0x01
#define EXT4_POLICY_FLAGS_PAD_16	0x02
#define EXT4_POLICY_FLAGS_PAD_32	0x03
#define EXT4_POLICY_FLAGS_PAD_MASK	0x03
#define EXT4_POLICY_FLAG_DIRECT_KEY	0x04	/* use master key directly */
#define EXT4_POLICY_FLAGS_VALID		0x07

/**
 * Encryption context for inode
 *
 * Protector format:
 *  1 byte: Protector format (1 = this version)
 *  1 byte: File contents encryption mode
 *  1 byte: File names encryption mode
 *  1 byte: Reserved
 *  8 bytes: Master Key descriptor
 *  16 bytes: Encryption Key derivation nonce
 */
struct ext4_encryption_context {
	char format;
	char contents_encryption_mode;
	char filenames_encryption_mode;
	char flags;
	char master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
	char nonce[EXT4_KEY_DERIVATION_NONCE_SIZE];
} __attribute__((__packed__));

/* Encryption parameters */
#define EXT4_MAX_KEY_SIZE 64
#define EXT4_AES_128_ECB_KEY_SIZE 16
#define EXT4_AES_256_XTS_KEY_SIZE 64

#define EXT4_KEY_DESC_PREFIX "ext4:"
#define EXT4_KEY_DESC_PREFIX_SIZE 5

/* This is passed in from userspace into the kernel keyring */
struct ext4_encryption_key {
        __u32 mode;
        char raw[EXT4_MAX_KEY_SIZE];
        __u32 size;
} __attribute__((__packed__));

struct ext4_crypt_info {

	/* The actual crypto transform used for encryption and decryption */
	struct crypto_ablkcipher *ci_ctfm;

	/*
	 * Encryption mode used for this inode.  It corresponds to either
	 * ci_data_mode or ci_filename_mode, depending on the inode type.
	 */
	struct ext4_crypt_mode *ci_mode;

	/*
	 * If non-NULL, then this inode uses a master key directly rather than a
	 * derived key, and ci_ctfm will equal ci_master_key->mk_ctfm.
	 * Otherwise, this inode uses a derived key.
	 */
	struct ext4_crypt_master_key *ci_master_key;

	/* fields from the ext4_crypt_context */
	char		ci_data_mode;
	char		ci_filename_mode;
	char		ci_flags;
	struct key	*ci_keyring_key;
	char ci_master_key_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
	char ci_nonce[EXT4_KEY_DERIVATION_NONCE_SIZE];
};

#define EXT4_CTX_REQUIRES_FREE_ENCRYPT_FL             0x00000001
#define EXT4_WRITE_PATH_FL			      0x00000002

struct ext4_crypto_ctx {
	union {
		struct {
			struct page *bounce_page;       /* Ciphertext page */
			struct page *control_page;      /* Original page  */
		} w;
		struct {
			struct bio *bio;
			struct work_struct work;
		} r;
		struct list_head free_list;     /* Free list */
	};
	char flags;                      /* Flags */
	char mode;                       /* Encryption mode for tfm */
};

struct ext4_completion_result {
	struct completion completion;
	int res;
};

#define DECLARE_EXT4_COMPLETION_RESULT(ecr) \
	struct ext4_completion_result ecr = { \
		COMPLETION_INITIALIZER((ecr).completion), 0 }

#define EXT4_FNAME_NUM_SCATTER_ENTRIES	4
#define EXT4_CRYPTO_BLOCK_SIZE		16
#define EXT4_FNAME_CRYPTO_DIGEST_SIZE	32

struct ext4_str {
	unsigned char *name;
	u32 len;
};

/**
 * For encrypted symlinks, the ciphertext length is stored at the beginning
 * of the string in little-endian format.
 */
struct ext4_encrypted_symlink_data {
	__le16 len;
	char encrypted_path[1];
} __attribute__((__packed__));

/**
 * This function is used to calculate the disk space required to
 * store a filename of length l in encrypted symlink format.
 */
static inline u32 encrypted_symlink_data_len(u32 l)
{
	if (l < EXT4_CRYPTO_BLOCK_SIZE)
		l = EXT4_CRYPTO_BLOCK_SIZE;
	return (l + sizeof(struct ext4_encrypted_symlink_data) - 1);
}

#define EXT4_CRYPT_MAX_IV_SIZE 32

union ext4_crypt_iv {
	struct {
		pgoff_t index;

		/* per-file nonce; only set in DIRECT_KEY mode */
		u8 nonce[EXT4_KEY_DERIVATION_NONCE_SIZE];
	};
	u8 raw[EXT4_CRYPT_MAX_IV_SIZE];
};

void ext4_crypt_generate_iv(union ext4_crypt_iv *iv, pgoff_t index,
			 const struct ext4_crypt_info *ci);

struct ext4_crypt_mode {
	const char *friendly_name;
	const char *cipher_str;
	int keysize;
	int ivsize;
	bool logged_impl_name;
};

#endif	/* _EXT4_CRYPTO_H */
