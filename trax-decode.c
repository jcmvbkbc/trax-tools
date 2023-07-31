#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define pr_info printf
#define pr_debug (void)

#define fallthrough

static const char * const branch_type_str[] = {
	"?",
	"normal",
	"exception",
	"loopback",
	"OCD return",
	"OCD exception",
};

struct trax_context {
	u32 addr;
	bool valid;
};

struct trax_msg {
	enum {
		TRAX_MSG_TYPE_IGNORE,
		TRAX_MSG_TYPE_UNDEF,
		TRAX_MSG_TYPE_IBM,
		TRAX_MSG_TYPE_FIRST = TRAX_MSG_TYPE_IBM,
		TRAX_MSG_TYPE_IBMS,
		TRAX_MSG_TYPE_SYNC,
		TRAX_MSG_TYPE_CORR,
		TRAX_MSG_TYPE_LAST = TRAX_MSG_TYPE_CORR,
	} type;
	enum {
		TRAX_MSG_BRANCH_UNDEF,
		TRAX_MSG_BRANCH_NORMAL,
		TRAX_MSG_BRANCH_NON_OCD_EXCEPTION,
		TRAX_MSG_BRANCH_LOOPBACK,
		TRAX_MSG_BRANCH_OCD_RETURN,
		TRAX_MSG_BRANCH_OCD_EXCEPTION,
	} branch_type;
	u16 icnt;
	u8 dcont;
	u32 addr;
	u64 timestamp;
};

struct trax_stream_context {
	u8 packet_buffer[15];
	u8 packet_size;		/* bits in the packet_buffer */
	struct trax_msg msg;
	u8 msg_packet;		/* current packet in the message */

	u8 scratch_type;
	u8 scratch_branch_type;
};

struct trax_field {
	u8 src_bits;
	u8 dst_bytes;
	u8 dst_offset;
	bool (*fixup)(struct trax_stream_context *ctx);
};

static bool trax_msg_type_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_type) {
	case 4:
		ctx->msg.type = TRAX_MSG_TYPE_IBM;
		return true;
	case 12:
		ctx->msg.type = TRAX_MSG_TYPE_IBMS;
		return true;
	case 9:
		ctx->msg.type = TRAX_MSG_TYPE_SYNC;
		return true;
	case 33:
		ctx->msg.type = TRAX_MSG_TYPE_CORR;
		return true;
	default:
		return false;
	}
}

#if 0
static bool trax_msg_branch_type_fixup_v4(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NORMAL;
		return true;
	case 1:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NON_OCD_EXCEPTION;
		return true;
	case 2:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_LOOPBACK;
		return true;
	case 4:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_OCD_RETURN;
		return true;
	case 6:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_OCD_EXCEPTION;
		return true;
	default:
		return false;
	}
}
#endif

static bool trax_msg_branch_type_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NORMAL;
		return true;
	case 1:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NON_OCD_EXCEPTION;
		return true;
	default:
		return false;
	}
}
static bool trax_msg_evcode_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0xa:
		return true;
	default:
		return false;
	}
}

static const struct trax_field trax_ibm_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_branch_type_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_ibms_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, msg.dcont),
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_branch_type_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_sync_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, msg.dcont),
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_corr_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_evcode_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_addr_packet[] = {
	{
		.dst_bytes = 4,
		.dst_offset = offsetof(struct trax_stream_context, msg.addr),
	},
};

static const struct trax_field trax_timestamp_packet[] = {
	{
		.dst_bytes = 8,
		.dst_offset = offsetof(struct trax_stream_context, msg.timestamp),
	},
};

static const struct {
	u8 max_packets;
	const struct trax_field *packet[3];
} trax_decode[] = {
	[TRAX_MSG_TYPE_IBM] = {
		.max_packets = 3,
		.packet = {
			trax_ibm_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_IBMS] = {
		.max_packets = 3,
		.packet = {
			trax_ibms_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_SYNC] = {
		.max_packets = 3,
		.packet = {
			trax_sync_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_CORR] = {
		.max_packets = 2,
		.packet = {
			trax_corr_packet0,
			trax_timestamp_packet,
		},
	},
};

static bool trax_decode_field(struct trax_stream_context *ctx,
			      const struct trax_field *field,
			      u32 start_bit)
{
	u32 i, n;

	memset((char *)ctx + field->dst_offset, 0, field->dst_bytes);
	if (ctx->packet_size - start_bit < field->src_bits) {
		pr_debug("%s: wanted %d bits, got %d - %d\n",
			 __func__, field->src_bits, ctx->packet_size, start_bit);
		return false;
	}

	n = field->src_bits ? field->src_bits : ctx->packet_size - start_bit;
	if (n > field->dst_bytes * 8)
		n = field->dst_bytes * 8;

	for (i = 0; i < n; i += 8) {
		u32 j = start_bit + i;
		u32 b = ctx->packet_buffer[j / 8] >> (j % 8);

		if (j % 8 && n - i > 8 - j % 8) {
			b |= ctx->packet_buffer[j / 8 + 1] << (8 - j % 8);
		}
		if (n - i < 8)
			b &= (1 << (n - i)) - 1;
		*((u8 *)ctx + field->dst_offset + i / 8) = b;
	}
#ifdef DEBUG
	print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE,
		       32, 1, ((u8 *)ctx + field->dst_offset), field->dst_bytes, false);
#endif

	if (field->fixup)
		return field->fixup(ctx);
	else
		return true;
}

static bool trax_decode_fields(struct trax_stream_context *ctx,
			       const struct trax_field *field)
{
	u32 i = 0;

	for (;;) {
		if (!trax_decode_field(ctx, field, i))
			return false;
		if (field->src_bits)
			i += field->src_bits;
		else
			return true;
		++field;
	}
}

static bool trax_decode_packet(struct trax_stream_context *ctx)
{
	pr_debug("%s: packet_size = %d\n", __func__, ctx->packet_size);
#ifdef DEBUG
	print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE,
		       32, 1, ctx->packet_buffer, (ctx->packet_size + 7) / 8, false);
#endif

	if (ctx->msg.type == TRAX_MSG_TYPE_UNDEF) {
		u32 i;

		for (i = TRAX_MSG_TYPE_FIRST; i <= TRAX_MSG_TYPE_LAST; ++i)
			if (trax_decode_fields(ctx, trax_decode[i].packet[0])) {
				ctx->msg_packet = 1;
				return true;
			}
		return false;
	} else {
	    if (ctx->msg_packet < trax_decode[ctx->msg.type].max_packets &&
		trax_decode_fields(ctx, trax_decode[ctx->msg.type].packet[ctx->msg_packet])) {
		    ++ctx->msg_packet;
		    return true;
	    } else {
		    ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
		    ctx->msg_packet = 0;
		    return false;
	    }
	}
}

static bool trax_decode_message(struct trax_context *trax_ctx,
				const struct trax_msg *msg)
{
	struct trax_context next = *trax_ctx;
	bool ret;

	switch (msg->type) {
	case TRAX_MSG_TYPE_IBM:
		pr_debug("%s:  IBM: type: %13s, icnt = %4d, u-addr = 0x%08x, tstamp = %lld\n",
			 __func__, branch_type_str[msg->branch_type],
			 msg->icnt, msg->addr, msg->timestamp);
		next.addr ^= msg->addr;
		trax_ctx->addr += msg->icnt;
		break;
	case TRAX_MSG_TYPE_IBMS:
		pr_debug("%s: IBMS: type: %13s, icnt = %4d, f-addr = 0x%08x, tstamp = %lld, dcont = %d\n",
			 __func__, branch_type_str[msg->branch_type],
			 msg->icnt, msg->addr, msg->timestamp, msg->dcont);
		next.addr = msg->addr;
		next.valid = true;
		trax_ctx->addr += msg->icnt;
		break;
	case TRAX_MSG_TYPE_SYNC:
		pr_debug("%s: SYNC: type: %13s, icnt = %4d, f-addr = 0x%08x, tstamp = %lld, dcont = %d\n",
			 __func__, "-",
			 msg->icnt, msg->addr, msg->timestamp, msg->dcont);
		next.addr = msg->addr;
		next.valid = true;
		break;
	case TRAX_MSG_TYPE_CORR:
		pr_debug("%s: CORR: type: %13s, icnt = %4d, ------ = 0x%08x, tstamp = %lld\n",
			 __func__, "-----",
			 msg->icnt, 0, msg->timestamp);
		trax_ctx->addr += msg->icnt;
		break;
	default:
		return true;
	}

	switch (msg->type) {
	case TRAX_MSG_TYPE_IBM:
		if (!next.valid)
			break;
		fallthrough;
	case TRAX_MSG_TYPE_IBMS:
		pr_info("%s: 0x%08x -> 0x%08x (%s) @ %lld\n",
			__func__, trax_ctx->addr, next.addr,
			branch_type_str[msg->branch_type], msg->timestamp);
		break;
	case TRAX_MSG_TYPE_SYNC:
		pr_info("%s: 0x%08x @ %lld\n",
			__func__, next.addr, msg->timestamp);
		break;
	default:
		break;
	}
	ret = true;

	*trax_ctx = next;
	return ret;
}

static bool trax_decode_stream(struct trax_context *trax_ctx,
			       struct trax_stream_context *stm_ctx, u32 data)
{
	u32 i;

	for (i = 0; i < sizeof(data); ++i) {
		u8 b = data & 0xff;
		u8 v = b >> 2;
		u8 packet_size = stm_ctx->packet_size;

		data >>= 8;
		if (stm_ctx->msg.type == TRAX_MSG_TYPE_IGNORE) {
			if ((b & 3) == 3) {
				pr_debug("%s: type = TRAX_MSG_TYPE_UNDEF\n", __func__);
				stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
				stm_ctx->packet_size = 0;
			}
		} else if (packet_size + 6 < sizeof(stm_ctx->packet_buffer) * 8) {
			if (!(packet_size % 8))
				stm_ctx->packet_buffer[packet_size / 8] = v;
			else
				stm_ctx->packet_buffer[packet_size / 8] |= v << (packet_size % 8);
			if (packet_size % 8 > 2)
				stm_ctx->packet_buffer[packet_size / 8 + 1] = v >> (8 - packet_size % 8);
			stm_ctx->packet_size += 6;

			if (b & 1) {
				bool ret = trax_decode_packet(stm_ctx);

				stm_ctx->packet_size = 0;
				if (!ret) {
					if (b & 2)
						stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
					else
						stm_ctx->msg.type = TRAX_MSG_TYPE_IGNORE;
				} else if (b & 2) {
					if (!trax_decode_message(trax_ctx, &stm_ctx->msg))
						return false;
					stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
				}
			}
		} else {
			pr_debug("%s: type = TRAX_MSG_TYPE_IGNORE, stm_ctx->packet_size = %d\n",
				 __func__, stm_ctx->packet_size);
			stm_ctx->msg.type = TRAX_MSG_TYPE_IGNORE;
		}
	}
	return true;
}

int main(void)
{
	struct trax_context trax_ctx = {0};
	struct trax_stream_context stm_ctx = {0};
	u32 data;

	while (read(STDIN_FILENO, &data, sizeof(data)) == sizeof(data)) {
		if (!trax_decode_stream(&trax_ctx, &stm_ctx, data))
			break;
	}
	return 0;
}
