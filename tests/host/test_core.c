// SPDX-License-Identifier: MIT
/*
 * Header-only parts of the LinuxKPI layer: lists, bit operations,
 * bitfields, byte order, error pointers, string helpers.  These are the
 * primitives the whole driver is built from; a bug here corrupts register
 * programming silently, so they are tested exhaustively against the Linux
 * semantics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/average.h>
#include <linux/overflow.h>
#include <asm/unaligned.h>
#include "test.h"

struct item {
	int v;
	struct list_head node;
	struct hlist_node hnode;
};

static void test_list(void)
{
	LIST_HEAD(head);
	struct item a = { .v = 1 }, b = { .v = 2 }, c = { .v = 3 }, *it, *tmp;
	int sum = 0, n = 0;

	CHECK(list_empty(&head));
	list_add_tail(&a.node, &head);
	list_add_tail(&b.node, &head);
	list_add(&c.node, &head);		/* c a b */
	CHECK(!list_empty(&head));
	CHECK_EQ(list_first_entry(&head, struct item, node)->v, 3);
	CHECK_EQ(list_last_entry(&head, struct item, node)->v, 2);
	list_for_each_entry(it, &head, node) {
		sum += it->v;
		n++;
	}
	CHECK_EQ(sum, 6);
	CHECK_EQ(n, 3);
	list_for_each_entry_safe(it, tmp, &head, node)
		if (it->v == 1)
			list_del_init(&it->node);
	CHECK_EQ(list_count_nodes(&head), 2);
	CHECK(list_empty(&a.node));
	list_move_tail(&c.node, &head);		/* b c */
	CHECK_EQ(list_first_entry(&head, struct item, node)->v, 2);
	CHECK(list_is_last(&c.node, &head));
	{
		LIST_HEAD(other);
		list_splice_tail_init(&head, &other);
		CHECK(list_empty(&head));
		CHECK_EQ(list_count_nodes(&other), 2);
	}
}

static void test_hlist(void)
{
	HLIST_HEAD(h);
	struct item a = { .v = 1 }, b = { .v = 2 }, *it;
	int n = 0;
	hlist_add_head(&a.hnode, &h);
	hlist_add_head(&b.hnode, &h);
	hlist_for_each_entry(it, &h, hnode)
		n += it->v;
	CHECK_EQ(n, 3);
	hlist_del_init(&a.hnode);
	CHECK(hlist_unhashed(&a.hnode));
	CHECK(!hlist_empty(&h));
	hlist_del(&b.hnode);
	CHECK(hlist_empty(&h));
}

static void test_bitops(void)
{
	DECLARE_BITMAP(bm, 130);
	unsigned long bit;
	int n = 0;

	bitmap_zero(bm, 130);
	CHECK(bitmap_empty(bm, 130));
	set_bit(0, bm);
	set_bit(64, bm);
	set_bit(129, bm);
	CHECK(test_bit(64, bm));
	CHECK(!test_bit(65, bm));
	CHECK_EQ(bitmap_weight(bm, 130), 3);
	CHECK_EQ(find_first_bit(bm, 130), 0);
	CHECK_EQ(find_next_bit(bm, 130, 1), 64);
	CHECK_EQ(find_next_bit(bm, 130, 65), 129);
	CHECK_EQ(find_next_bit(bm, 129, 65), 129);	/* not found == size */
	CHECK_EQ(find_first_zero_bit(bm, 130), 1);
	for_each_set_bit(bit, bm, 130)
		n++;
	CHECK_EQ(n, 3);
	CHECK(test_and_clear_bit(64, bm));
	CHECK(!test_and_clear_bit(64, bm));
	CHECK(!test_and_set_bit(64, bm));
	CHECK(test_and_set_bit(64, bm));

	CHECK_EQ(hweight8(0xff), 8);
	CHECK_EQ(hweight16(0x0f0f), 8);
	CHECK_EQ(hweight32(0x80000001), 2);
	CHECK_EQ(__ffs(0x80), 7);
	CHECK_EQ(__fls(0x80), 7);
	CHECK_EQ(ffs(0), 0);
	CHECK_EQ(ffs(0x10), 5);
	CHECK_EQ(fls(0), 0);
	CHECK_EQ(fls(0x10), 5);
	CHECK_EQ(fls64(1ULL << 40), 41);
	CHECK_EQ(ilog2(1), 0);
	CHECK_EQ(ilog2(4096), 12);
	CHECK_EQ(roundup_pow_of_two(5), 8);
	CHECK_EQ(rounddown_pow_of_two(5), 4);
	CHECK(is_power_of_2(1024));
	CHECK(!is_power_of_2(1000));
	CHECK_EQ(GENMASK(7, 4), 0xf0);
	CHECK_EQ(GENMASK_ULL(35, 32), 0xf00000000ULL);
	CHECK_EQ(BIT(31), 0x80000000UL);
	CHECK_EQ(sign_extend32(0x80, 7), -128);
	CHECK_EQ(DIV_ROUND_UP(10, 3), 4);
	CHECK_EQ(ALIGN(13, 8), 16);
	CHECK_EQ(round_down(13, 8), 8);
	CHECK_EQ(roundup(13, 5), 15);
	CHECK_EQ(min(3, 5), 3);
	CHECK_EQ(max_t(u8, 3, 250), 250);
	CHECK_EQ(clamp(300, 0, 255), 255);
	CHECK_EQ(upper_32_bits(0x123456789ULL), 1);
	CHECK_EQ(lower_32_bits(0x123456789ULL), 0x23456789);
	CHECK_EQ(get_order(4096), 0);
	CHECK_EQ(get_order(8192), 1);
	CHECK_EQ(get_order(4097), 1);
}

#define TEST_FIELD	GENMASK(11, 4)
#define TEST_FIELD_HI	GENMASK_ULL(47, 40)

static void test_bitfield(void)
{
	u32 reg = 0;
	u64 reg64 = 0;
	__le32 le;
	__le16 le16;

	reg |= FIELD_PREP(TEST_FIELD, 0xab);
	CHECK_EQ(reg, 0xab0);
	CHECK_EQ(FIELD_GET(TEST_FIELD, 0xfab0), 0xab);
	CHECK_EQ(FIELD_MAX(TEST_FIELD), 0xff);
	CHECK(FIELD_FIT(TEST_FIELD, 0xff));
	CHECK(!FIELD_FIT(TEST_FIELD, 0x100));
	reg64 = FIELD_PREP(TEST_FIELD_HI, 0x5a);
	CHECK_EQ(FIELD_GET(TEST_FIELD_HI, reg64), 0x5a);

	CHECK_EQ(u32_encode_bits(0x3, GENMASK(5, 4)), 0x30);
	CHECK_EQ(u32_get_bits(0x30, GENMASK(5, 4)), 0x3);
	CHECK_EQ(u8_encode_bits(1, BIT(7)), 0x80);
	CHECK_EQ(u16_get_bits(0xf000, GENMASK(15, 12)), 0xf);
	le = le32_encode_bits(0xab, TEST_FIELD);
	CHECK_EQ(le32_to_cpu(le), 0xab0);
	CHECK_EQ(le32_get_bits(le, TEST_FIELD), 0xab);
	le16 = cpu_to_le16(0x1234);
	CHECK_EQ(le16_get_bits(le16, GENMASK(7, 0)), 0x34);
	le32p_replace_bits(&le, 0x11, TEST_FIELD);
	CHECK_EQ(le32_get_bits(le, TEST_FIELD), 0x11);
	CHECK_EQ(be32_encode_bits(1, BIT(0)), cpu_to_be32(1));
	CHECK_EQ(field_prep(0xf0, 0xa), 0xa0);
	CHECK_EQ(field_get(0xf0, 0xab), 0xa);
}

static void test_byteorder(void)
{
	u8 buf[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };

	CHECK_EQ(le16_to_cpu(cpu_to_le16(0x1234)), 0x1234);
	CHECK_EQ(be16_to_cpu(cpu_to_be16(0x1234)), 0x1234);
	CHECK_EQ((u16)cpu_to_be16(0x1234), 0x3412);
	CHECK_EQ((u32)cpu_to_be32(0x12345678), 0x78563412);
	CHECK_EQ(be64_to_cpu(cpu_to_be64(0x0102030405060708ULL)), 0x0102030405060708ULL);
	CHECK_EQ(get_unaligned_le16(buf), 0x0201);
	CHECK_EQ(get_unaligned_be16(buf), 0x0102);
	CHECK_EQ(get_unaligned_le32(buf + 1), 0x05040302);
	CHECK_EQ(get_unaligned_be32(buf), 0x01020304);
	CHECK_EQ(get_unaligned_le24(buf), 0x030201);
	put_unaligned_le32(0xdeadbeef, buf + 2);
	CHECK_EQ(buf[2], 0xef);
	CHECK_EQ(buf[5], 0xde);
	put_unaligned_be16(0xcafe, buf);
	CHECK_EQ(buf[0], 0xca);
	CHECK_EQ(swab16(0x1234), 0x3412);
	CHECK_EQ(swab32(0x12345678), 0x78563412);
	CHECK_EQ(le16_to_cpup((__le16 *)buf), 0xfeca);
}

static void test_err(void)
{
	void *p = ERR_PTR(-ENOMEM);
	CHECK(IS_ERR(p));
	CHECK_EQ(PTR_ERR(p), -ENOMEM);
	CHECK(IS_ERR_OR_NULL(NULL));
	CHECK(!IS_ERR(&p));
	CHECK_EQ(PTR_ERR_OR_ZERO(&p), 0);
	CHECK_EQ(PTR_ERR_OR_ZERO(ERR_PTR(-EINVAL)), -EINVAL);
	/* Linux errno values, not the host's */
	CHECK_EQ(ETIMEDOUT, 110);
	CHECK_EQ(EOPNOTSUPP, 95);
	CHECK_EQ(ENOTSUPP, 524);
}

static void test_string(void)
{
	char buf[8];
	char line[] = "  abc  ";
	CHECK_EQ(strscpy(buf, "hello", sizeof(buf)), 5);
	CHECK(!strcmp(buf, "hello"));
	CHECK_EQ(strscpy(buf, "much longer", sizeof(buf)), -E2BIG);
	CHECK(!strcmp(buf, "much lo"));
	CHECK_EQ(strscpy(buf, "ab"), 2);	/* two-argument form */
	CHECK(!strcmp(strim(line), "abc"));
	CHECK(strstarts("mediatek/mt7925/x.bin", "mediatek/"));
	CHECK(memchr_inv("\0\0\1", 0, 3) != NULL);
	CHECK(memchr_inv("\0\0\0", 0, 3) == NULL);
	CHECK_EQ(hex_to_bin('f'), 15);
	CHECK_EQ(hex_to_bin('g'), -1);
	{
		u8 bin[2];
		CHECK_EQ(hex2bin(bin, "a1ff", 2), 0);
		CHECK_EQ(bin[0], 0xa1);
		CHECK_EQ(bin[1], 0xff);
	}
	CHECK_EQ(isalpha('x'), 1);
	CHECK_EQ(isdigit('x'), 0);
	CHECK_EQ(toupper('a'), 'A');
	CHECK_EQ(DIV_ROUND_CLOSEST(7, 2), 4);
}

DECLARE_EWMA(sig, 10, 8)

static void test_ewma(void)
{
	struct ewma_sig e;
	int i;
	ewma_sig_init(&e);
	CHECK_EQ(ewma_sig_read(&e), 0);
	ewma_sig_add(&e, 100);
	CHECK_EQ(ewma_sig_read(&e), 100);
	for (i = 0; i < 64; i++)
		ewma_sig_add(&e, 20);
	CHECK(ewma_sig_read(&e) >= 20 && ewma_sig_read(&e) <= 21);
}

static void test_overflow(void)
{
	size_t r;
	CHECK(!check_mul_overflow((size_t)4, (size_t)8, &r));
	CHECK_EQ(r, 32);
	CHECK(check_mul_overflow(SIZE_MAX, (size_t)2, &r));
	CHECK_EQ(array_size(10, 10), 100);
	CHECK_EQ(array_size(SIZE_MAX, 2), SIZE_MAX);
	struct flex { int n; int data[]; };
	CHECK_EQ(struct_size((struct flex *)0, data, 3), sizeof(struct flex) + 3 * sizeof(int));
}

int main(void)
{
	RUN(test_list);
	RUN(test_hlist);
	RUN(test_bitops);
	RUN(test_bitfield);
	RUN(test_byteorder);
	RUN(test_err);
	RUN(test_string);
	RUN(test_ewma);
	RUN(test_overflow);
	return test_report("test_core");
}
