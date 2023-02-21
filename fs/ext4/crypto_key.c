/*
 * linux/fs/ext4/crypto_key.c
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption key functions for ext4
 *
 * Written by Michael Halcrow, Ildar Muslukhov, and Uday Savagaonkar, 2015.
 */

#include <keys/encrypted-type.h>
#include <keys/user-type.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <uapi/linux/keyctl.h>
#include <linux/hashtable.h>
#include <crypto/algapi.h>

#include "ext4.h"
#include "xattr.h"

/* Table of keys referenced by EXT4_POLICY_FLAG_DIRECT_KEY policies */
static DEFINE_HASHTABLE(ext4_crypt_master_keys, 6); /* 6 bits = 64 buckets */
static DEFINE_SPINLOCK(ext4_crypt_master_keys_lock);

static void derive_crypt_complete(struct crypto_async_request *req, int rc)
{
	struct ext4_completion_result *ecr = req->data;

	if (rc == -EINPROGRESS)
		return;

	ecr->res = rc;
	complete(&ecr->completion);
}

/**
 * ext4_derive_key_aes() - Derive a key using AES-128-ECB
 * @deriving_key: Encryption key used for derivation.
 * @source_key:   Source key to which to apply derivation.
 * @derived_key:  Derived key.
 *
 * Return: Zero on success; non-zero otherwise.
 */
static int ext4_derive_key_aes(char deriving_key[EXT4_AES_128_ECB_KEY_SIZE],
			       char source_key[EXT4_AES_256_XTS_KEY_SIZE],
			       char derived_key[EXT4_AES_256_XTS_KEY_SIZE])
{
	int res = 0;
	struct ablkcipher_request *req = NULL;
	DECLARE_EXT4_COMPLETION_RESULT(ecr);
	struct scatterlist src_sg, dst_sg;
	struct crypto_ablkcipher *tfm = crypto_alloc_ablkcipher("ecb(aes)", 0,
								0);

	if (IS_ERR(tfm)) {
		res = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}
	crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		res = -ENOMEM;
		goto out;
	}
	ablkcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			derive_crypt_complete, &ecr);
	res = crypto_ablkcipher_setkey(tfm, deriving_key,
				       EXT4_AES_128_ECB_KEY_SIZE);
	if (res < 0)
		goto out;
	sg_init_one(&src_sg, source_key, EXT4_AES_256_XTS_KEY_SIZE);
	sg_init_one(&dst_sg, derived_key, EXT4_AES_256_XTS_KEY_SIZE);
	ablkcipher_request_set_crypt(req, &src_sg, &dst_sg,
				     EXT4_AES_256_XTS_KEY_SIZE, NULL);
	res = crypto_ablkcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}

out:
	if (req)
		ablkcipher_request_free(req);
	if (tfm)
		crypto_free_ablkcipher(tfm);
	return res;
}

/* Master key referenced by EXT4_POLICY_FLAG_DIRECT_KEY policy */
struct ext4_crypt_master_key {
	struct hlist_node mk_node;
	atomic_t mk_refcount;
	const struct ext4_crypt_mode *mk_mode;
	struct crypto_ablkcipher *mk_ctfm;
	u8 mk_descriptor[EXT4_KEY_DESCRIPTOR_SIZE];
	u8 mk_raw[EXT4_MAX_KEY_SIZE];
};

static void free_master_key(struct ext4_crypt_master_key *mk)
{
	if (mk) {
		crypto_free_ablkcipher(mk->mk_ctfm);
		kzfree(mk);
	}
}

static void put_master_key(struct ext4_crypt_master_key *mk)
{
	if (!atomic_dec_and_lock(&mk->mk_refcount, &ext4_crypt_master_keys_lock))
		return;
	hash_del(&mk->mk_node);
	spin_unlock(&ext4_crypt_master_keys_lock);

	free_master_key(mk);
}


void ext4_free_crypt_info(struct ext4_crypt_info *ci)
{
	if (!ci)
		return;

	if (ci->ci_keyring_key)
		key_put(ci->ci_keyring_key);

	if (ci->ci_master_key)
		put_master_key(ci->ci_master_key);
	else
		crypto_free_ablkcipher(ci->ci_ctfm);

	kmem_cache_free(ext4_crypt_info_cachep, ci);
}

void ext4_free_encryption_info(struct inode *inode,
			       struct ext4_crypt_info *ci)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_crypt_info *prev;

	if (ci == NULL)
		ci = ACCESS_ONCE(ei->i_crypt_info);
	if (ci == NULL)
		return;
	prev = cmpxchg(&ei->i_crypt_info, ci, NULL);
	if (prev != ci)
		return;

	ext4_free_crypt_info(ci);
}

static struct ext4_crypt_mode available_modes[] = {
	[EXT4_ENCRYPTION_MODE_AES_256_XTS] = {
		.friendly_name = "AES-256-XTS",
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	[EXT4_ENCRYPTION_MODE_AES_256_CTS] = {
		.friendly_name = "AES-256-CTS-CBC",
		.cipher_str = "cts(cbc(aes))",
		.keysize = 32,
		.ivsize = 16,
	},
	[EXT4_ENCRYPTION_MODE_SPECK128_256_XTS] = {

		.friendly_name = "SPECK128-256-XTS",
		.cipher_str = "xts(speck128)",
		.keysize = 64,
		.ivsize = 16,
	},
	[EXT4_ENCRYPTION_MODE_SPECK128_256_CTS] = {
		.friendly_name = "SPECK128-256-CTS-CBC",
		.cipher_str = "cts(cbc(speck128))",
		.keysize = 32,
		.ivsize = 16,
	},
	[EXT4_ENCRYPTION_MODE_ADIANTUM] = {
		.friendly_name = "Adiantum",
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.ivsize = 32,
	},
};

static struct ext4_crypt_mode *
select_encryption_mode(const struct ext4_crypt_info *ci, const struct inode *inode)
{
	if (!ext4_valid_enc_modes(ci->ci_data_mode, ci->ci_filename_mode)) {
		printk("inode %lu uses unsupported encryption modes (contents mode %d, filenames mode %d)",
			     inode->i_ino, ci->ci_data_mode,
			     ci->ci_filename_mode);
		return ERR_PTR(-EINVAL);
	}

	if (S_ISREG(inode->i_mode))
		return &available_modes[ci->ci_data_mode];

	if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
		return &available_modes[ci->ci_filename_mode];

	WARN_ONCE(1, "ext4_crypt: filesystem tried to load encryption info for inode %lu, which is not encryptable (file type %d)\n",
		  inode->i_ino, (inode->i_mode & S_IFMT));
	return ERR_PTR(-EINVAL);
}

/* Allocate and key a symmetric cipher object for the given encryption mode */
static struct crypto_ablkcipher *
allocate_ablkcipher_for_mode(struct ext4_crypt_mode *mode, const u8 *raw_key,
			   const struct inode *inode)
{
	struct crypto_ablkcipher *ctfm;
	int err;

	ctfm = crypto_alloc_ablkcipher(mode->cipher_str, 0, 0);
	if (!ctfm || IS_ERR(ctfm)) {
		err = ctfm ? PTR_ERR(ctfm) : -ENOMEM;
		printk(KERN_DEBUG
		       "%s: error %d (inode %u) allocating crypto tfm\n",
		       __func__, err, (unsigned) inode->i_ino);
		return ERR_PTR(err);
	}
	if (unlikely(!mode->logged_impl_name)) {
		/*
		 * fscrypt performance can vary greatly depending on which
		 * crypto algorithm implementation is used.  Help people debug
		 * performance problems by logging the ->cra_driver_name the
		 * first time a mode is used.  Note that multiple threads can
		 * race here, but it doesn't really matter.
		 */
		mode->logged_impl_name = true;
		pr_info("ext4_crypt: %s using implementation \"%s\"\n",
			mode->friendly_name,
			crypto_tfm_alg_driver_name(crypto_ablkcipher_tfm(ctfm)));
	}

	crypto_ablkcipher_clear_flags(ctfm, ~0);
	crypto_tfm_set_flags(crypto_ablkcipher_tfm(ctfm),
			     CRYPTO_TFM_REQ_WEAK_KEY);
	err = crypto_ablkcipher_setkey(ctfm, raw_key,
				       mode->keysize);
	if (err)
		goto err_free_tfm;

	return ctfm;

err_free_tfm:
	crypto_free_ablkcipher(ctfm);
	return ERR_PTR(err);
}

/*
 * Find/insert the given master key into the ext4_crypt_master_keys table.  If
 * found, it is returned with elevated refcount, and 'to_insert' is freed if
 * non-NULL.  If not found, 'to_insert' is inserted and returned if it's
 * non-NULL; otherwise NULL is returned.
 */
static struct ext4_crypt_master_key *
find_or_insert_master_key(struct ext4_crypt_master_key *to_insert,
			  const u8 *raw_key, const struct ext4_crypt_mode *mode,
			  const struct ext4_crypt_info *ci)
{
	unsigned long hash_key;
	struct ext4_crypt_master_key *mk;

	/*
	 * Careful: to avoid potentially leaking secret key bytes via timing
	 * information, we must key the hash table by descriptor rather than by
	 * raw key, and use crypto_memneq() when comparing raw keys.
	 */

	BUILD_BUG_ON(sizeof(hash_key) > EXT4_KEY_DESCRIPTOR_SIZE);
	memcpy(&hash_key, ci->ci_master_key_descriptor, sizeof(hash_key));

	spin_lock(&ext4_crypt_master_keys_lock);
	hash_for_each_possible(ext4_crypt_master_keys, mk, mk_node, hash_key) {
		if (memcmp(ci->ci_master_key_descriptor, mk->mk_descriptor,
			   EXT4_KEY_DESCRIPTOR_SIZE) != 0)
			continue;
		if (mode != mk->mk_mode)
			continue;
		if (crypto_memneq(raw_key, mk->mk_raw, mode->keysize))
			continue;
		/* using existing tfm with same (descriptor, mode, raw_key) */
		atomic_inc(&mk->mk_refcount);
		spin_unlock(&ext4_crypt_master_keys_lock);
		free_master_key(to_insert);
		return mk;
	}
	if (to_insert)
		hash_add(ext4_crypt_master_keys, &to_insert->mk_node, hash_key);
	spin_unlock(&ext4_crypt_master_keys_lock);
	return to_insert;
}

/* Prepare to encrypt directly using the master key in the given mode */
static struct ext4_crypt_master_key *
ext4_crypt_get_master_key(const struct ext4_crypt_info *ci, struct ext4_crypt_mode *mode,
		       const u8 *raw_key, const struct inode *inode)
{
	struct ext4_crypt_master_key *mk;
	int err;

	/* Is there already a tfm for this key? */
	mk = find_or_insert_master_key(NULL, raw_key, mode, ci);
	if (mk)
		return mk;

	/* Nope, allocate one. */
	mk = kzalloc(sizeof(*mk), GFP_NOFS);
	if (!mk)
		return ERR_PTR(-ENOMEM);
	atomic_set(&mk->mk_refcount, 1);
	mk->mk_mode = mode;
	mk->mk_ctfm = allocate_ablkcipher_for_mode(mode, raw_key, inode);
	if (IS_ERR(mk->mk_ctfm)) {
		err = PTR_ERR(mk->mk_ctfm);
		mk->mk_ctfm = NULL;
		goto err_free_mk;
	}
	memcpy(mk->mk_descriptor, ci->ci_master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	memcpy(mk->mk_raw, raw_key, mode->keysize);

	return find_or_insert_master_key(mk, raw_key, mode, ci);

err_free_mk:
	free_master_key(mk);
	return ERR_PTR(err);
}

/*
 * Given the encryption mode and key (normally the derived key, but for
 * EXT4_POLICY_FLAG_DIRECT_KEY mode it's the master key), set up the inode's
 * symmetric cipher transform object(s).
 */
static int setup_crypto_transform(struct ext4_crypt_info *ci,
				  struct ext4_crypt_mode *mode,
				  const u8 *raw_key, const struct inode *inode)
{
	struct ext4_crypt_master_key *mk;
	struct crypto_ablkcipher *ctfm;

	if (ci->ci_flags & EXT4_POLICY_FLAG_DIRECT_KEY) {
		mk = ext4_crypt_get_master_key(ci, mode, raw_key, inode);
		if (IS_ERR(mk))
			return PTR_ERR(mk);
		ctfm = mk->mk_ctfm;
	} else {
		mk = NULL;
		ctfm = allocate_ablkcipher_for_mode(mode, raw_key, inode);
		if (IS_ERR(ctfm))
			return PTR_ERR(ctfm);
	}
	ci->ci_master_key = mk;
	ci->ci_ctfm = ctfm;

	return 0;
}

int _ext4_get_encryption_info(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_crypt_info *crypt_info;
	char full_key_descriptor[EXT4_KEY_DESC_PREFIX_SIZE +
				 (EXT4_KEY_DESCRIPTOR_SIZE * 2) + 1];
	struct key *keyring_key = NULL;
	struct ext4_encryption_key *master_key;
	struct ext4_encryption_context ctx;
	struct user_key_payload *ukp;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	char raw_key[EXT4_MAX_KEY_SIZE];
	struct ext4_crypt_mode *mode;
	int res = 0;
	unsigned char isDirectKey = 0;

	if (!ext4_read_workqueue) {
		res = ext4_init_crypto();
		if (res)
			return res;
	}

retry:
	crypt_info = ACCESS_ONCE(ei->i_crypt_info);
	if (crypt_info) {
		if (!crypt_info->ci_keyring_key ||
		    key_validate(crypt_info->ci_keyring_key) == 0)
			return 0;
		ext4_free_encryption_info(inode, crypt_info);
		goto retry;
	}

	res = ext4_xattr_get(inode, EXT4_XATTR_INDEX_ENCRYPTION,
				 EXT4_XATTR_NAME_ENCRYPTION_CONTEXT,
				 &ctx, sizeof(ctx));
	if (res < 0) {
		if (!DUMMY_ENCRYPTION_ENABLED(sbi))
			return res;
		ctx.contents_encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
		ctx.filenames_encryption_mode =
			EXT4_ENCRYPTION_MODE_AES_256_CTS;
		ctx.flags = 0;
	} else if (res != sizeof(ctx))
		return -EINVAL;
	res = 0;

	crypt_info = kmem_cache_zalloc(ext4_crypt_info_cachep, GFP_KERNEL);
	if (!crypt_info)
		return -ENOMEM;

	crypt_info->ci_flags = ctx.flags;
	crypt_info->ci_data_mode = ctx.contents_encryption_mode;
	crypt_info->ci_filename_mode = ctx.filenames_encryption_mode;
	crypt_info->ci_keyring_key = NULL;
	memcpy(crypt_info->ci_master_key_descriptor, ctx.master_key_descriptor,
	       EXT4_KEY_DESCRIPTOR_SIZE);
	memcpy(crypt_info->ci_nonce, ctx.nonce, EXT4_KEY_DERIVATION_NONCE_SIZE);

	mode = select_encryption_mode(crypt_info, inode);
	if (IS_ERR(mode)) {
		res = -ENOKEY;
		goto out;
	}
	crypt_info->ci_mode = mode;

	if (DUMMY_ENCRYPTION_ENABLED(sbi)) {
		memset(raw_key, 0x42, EXT4_AES_256_XTS_KEY_SIZE);
		goto got_key;
	}
	memcpy(full_key_descriptor, EXT4_KEY_DESC_PREFIX,
	       EXT4_KEY_DESC_PREFIX_SIZE);
	sprintf(full_key_descriptor + EXT4_KEY_DESC_PREFIX_SIZE,
		"%*phN", EXT4_KEY_DESCRIPTOR_SIZE,
		ctx.master_key_descriptor);
	full_key_descriptor[EXT4_KEY_DESC_PREFIX_SIZE +
			    (2 * EXT4_KEY_DESCRIPTOR_SIZE)] = '\0';
	keyring_key = request_key(&key_type_logon, full_key_descriptor, NULL);
	if (IS_ERR(keyring_key)) {
		res = PTR_ERR(keyring_key);
		keyring_key = NULL;
		goto out;
	}
	crypt_info->ci_keyring_key = keyring_key;
	if (keyring_key->type != &key_type_logon) {
		printk_once(KERN_WARNING
			    "ext4: key type must be logon\n");
		res = -ENOKEY;
		goto out;
	}
	down_read(&keyring_key->sem);
	ukp = ((struct user_key_payload *)keyring_key->payload.data);
	if (!ukp) {
		/* key was revoked before we acquired its semaphore */
		res = -EKEYREVOKED;
		up_read(&keyring_key->sem);
		goto out;
	}
	if (ukp->datalen != sizeof(struct ext4_encryption_key)) {
		res = -EINVAL;
		up_read(&keyring_key->sem);
		goto out;
	}
	master_key = (struct ext4_encryption_key *)ukp->data;
	BUILD_BUG_ON(EXT4_AES_128_ECB_KEY_SIZE !=
		     EXT4_KEY_DERIVATION_NONCE_SIZE);
	if (master_key->size != EXT4_AES_256_XTS_KEY_SIZE) {
		printk_once(KERN_WARNING
			    "ext4: key size incorrect: %d\n",
			    master_key->size);
		res = -ENOKEY;
		up_read(&keyring_key->sem);
		goto out;
	}
	isDirectKey = ctx.flags & EXT4_POLICY_FLAG_DIRECT_KEY;
	if(!isDirectKey)
		res = ext4_derive_key_aes(ctx.nonce, master_key->raw,
				  raw_key);
	up_read(&keyring_key->sem);
	if (res)
		goto out;
got_key:
	res = setup_crypto_transform(crypt_info, mode, isDirectKey ? master_key->raw : raw_key, inode);
	if (res)
		goto out;
	if(!isDirectKey)
		memzero_explicit(raw_key, sizeof(raw_key));
	if (cmpxchg(&ei->i_crypt_info, NULL, crypt_info) != NULL) {
		ext4_free_crypt_info(crypt_info);
		goto retry;
	}
	return 0;

out:
	if (res == -ENOKEY)
		res = 0;
	ext4_free_crypt_info(crypt_info);
	if(!isDirectKey)
		memzero_explicit(raw_key, sizeof(raw_key));
	return res;
}

int ext4_has_encryption_key(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	return (ei->i_crypt_info != NULL);
}
