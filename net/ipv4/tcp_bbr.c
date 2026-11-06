#include <linux/module.h>
#include <net/tcp.h>
#include <linux/inet_diag.h>
#include <linux/inet.h>
#include <linux/random.h>
#include <linux/win_minmax.h>

#define BBR_VERSION		3

#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)

#define BBR_SCALE 8
#define BBR_UNIT (1 << BBR_SCALE)

enum bbr_mode {
	BBR_STARTUP,
	BBR_DRAIN,
	BBR_PROBE_BW,
	BBR_PROBE_RTT,
};

enum bbr_ack_phase {
	BBR_ACKS_INIT,
	BBR_ACKS_REFILLING,
	BBR_ACKS_PROBE_STARTING,
	BBR_ACKS_PROBE_FEEDBACK,
	BBR_ACKS_PROBE_STOPPING,
};

enum bbr_pacing_gain_phase {
	BBR_BW_PROBE_UP		= 0,
	BBR_BW_PROBE_DOWN	= 1,
	BBR_BW_PROBE_CRUISE	= 2,
	BBR_BW_PROBE_REFILL	= 3,
};

struct bbr {
	u64	ack_epoch_mstamp;
	u32	min_rtt_us;
	u32	min_rtt_stamp;
	union {
		u32	probe_rtt_done_stamp;
		u32	probe_deadline_stamp;
	};
	u32	probe_rtt_min_us;
	u32	probe_rtt_min_stamp;
	u32	next_rtt_delivered;
	u32	prior_cwnd;
	u32	full_bw;
	u32	loss_round_delivered;
	u32	undo_bw_lo;
	u32	undo_inflight_lo;
	u32	undo_inflight_hi;
	u32	bw_latest;
	u32	bw_lo;
	u32	bw_hi[2];
	u32	inflight_latest;
	u32	inflight_lo;
	u32	inflight_hi;
	u16	extra_acked[2];
	u16	bw_probe_up_cnt;
	u16	bw_probe_up_acks;
	u32	mode:2,
		prev_ca_state:3,
		round_start:1,
		bw_probe_up_rounds:5,
		try_fast_path:1,
		idle_restart:1,
		probe_rtt_round_done:1,
		init_cwnd:7,
		full_bw_reached:1,
		full_bw_cnt:2,
		cycle_idx:2,
		has_seen_rtt:1,
		full_bw_now:1,
		loss_in_cycle:1,
		bw_probe_samples:1,
		prev_probe_too_high:1,
		stopped_risky_probe:1;
	u32	pacing_gain:10,
		cwnd_gain:10,
		rounds_since_probe:8,
		loss_round_start:1,
		loss_in_round:1,
		initialized:1,
		extra_acked_win_idx:1;
	u32	ack_epoch_acked:20,
		extra_acked_win_rtts:5,
		ack_phase:3,
		loss_events_in_round:4;
};

struct bbr_context {
	u32 sample_bw;
};

static const u32 bbr_min_rtt_win_sec = 10;
static const u32 bbr_probe_rtt_mode_ms = 200;
static const u32 bbr_probe_rtt_win_ms = 5000;
static const u32 bbr_probe_rtt_cwnd_gain = BBR_UNIT * 1 / 2;
static const u32 bbr_tso_rtt_shift = 9;
static const int bbr_min_tso_rate = 1200000;
static const int bbr_pacing_margin_percent = 1;

static const int bbr_startup_pacing_gain = BBR_UNIT * 277 / 100 + 1;
static const int bbr_startup_cwnd_gain = BBR_UNIT * 2;
static const int bbr_drain_gain = BBR_UNIT * 1000 / 2885;
static const int bbr_cwnd_gain = BBR_UNIT * 2;

static const int bbr_pacing_gain[] = {
	BBR_UNIT * 5 / 4,
	BBR_UNIT * 91 / 100,
	BBR_UNIT,
	BBR_UNIT,
};

static const u32 bbr_cwnd_min_target = 4;
static const u32 bbr_full_bw_thresh = BBR_UNIT * 5 / 4;
static const u32 bbr_full_bw_cnt = 3;

static const int bbr_extra_acked_gain = BBR_UNIT;
static const u32 bbr_extra_acked_win_rtts = 5;
static const u32 bbr_ack_epoch_acked_reset_thresh = 1U << 20;
static const u32 bbr_extra_acked_max_us = 100 * 1000;

static const u32 bbr_beta = BBR_UNIT * 30 / 100;
static const u32 bbr_loss_thresh = BBR_UNIT * 2 / 100;
static const u32 bbr_full_loss_cnt = 6;
static const u32 bbr_inflight_headroom = BBR_UNIT * 15 / 100;
static const u32 bbr_bw_probe_cwnd_gain = 1;
static const u32 bbr_bw_probe_max_rounds = 63;
static const u32 bbr_bw_probe_rand_rounds = 2;
static const u32 bbr_bw_probe_base_us = 2 * USEC_PER_SEC;
static const u32 bbr_bw_probe_rand_us = 1 * USEC_PER_SEC;
static const bool bbr_fast_path = true;

static void bbr_check_probe_rtt_done(struct sock *sk);
static void bbr_exit_probe_rtt(struct sock *sk);
static void bbr_reset_congestion_signals(struct sock *sk);

static bool bbr_full_bw_reached(const struct sock *sk)
{
	const struct bbr *bbr = inet_csk_ca(sk);

	return bbr->full_bw_reached;
}

static u32 bbr_max_bw(const struct sock *sk)
{
	const struct bbr *bbr = inet_csk_ca(sk);

	return max(bbr->bw_hi[0], bbr->bw_hi[1]);
}

static u32 bbr_bw(const struct sock *sk)
{
	const struct bbr *bbr = inet_csk_ca(sk);

	return min(bbr_max_bw(sk), bbr->bw_lo);
}

static u16 bbr_extra_acked(const struct sock *sk)
{
	const struct bbr *bbr = inet_csk_ca(sk);

	return max(bbr->extra_acked[0], bbr->extra_acked[1]);
}

static u64 bbr_rate_bytes_per_sec(struct sock *sk, u64 rate, int gain,
				  int margin)
{
	unsigned int mss = tcp_sk(sk)->mss_cache;

	rate *= mss;
	rate *= gain;
	rate >>= BBR_SCALE;
	rate *= USEC_PER_SEC / 100 * (100 - margin);
	rate >>= BW_SCALE;
	return max(rate, 1ULL);
}

static u64 bbr_bw_bytes_per_sec(struct sock *sk, u64 rate)
{
	return bbr_rate_bytes_per_sec(sk, rate, BBR_UNIT, 0);
}

static unsigned long bbr_bw_to_pacing_rate(struct sock *sk, u32 bw, int gain)
{
	u64 rate = bw;

	rate = bbr_rate_bytes_per_sec(sk, rate, gain,
				      bbr_pacing_margin_percent);
	rate = min_t(u64, rate, READ_ONCE(sk->sk_max_pacing_rate));
	return rate;
}

static void bbr_init_pacing_rate_from_rtt(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u64 bw;
	u32 rtt_us;

	if (tp->srtt_us) {
		rtt_us = max(tp->srtt_us >> 3, 1U);
		bbr->has_seen_rtt = 1;
	} else {
		rtt_us = USEC_PER_MSEC;
	}
	bw = (u64)tp->snd_cwnd * BW_UNIT;
	do_div(bw, rtt_us);
	WRITE_ONCE(sk->sk_pacing_rate,
		   bbr_bw_to_pacing_rate(sk, bw, bbr_startup_pacing_gain));
}

static void bbr_set_pacing_rate(struct sock *sk, u32 bw, int gain)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	unsigned long rate = bbr_bw_to_pacing_rate(sk, bw, gain);

	if (unlikely(!bbr->has_seen_rtt && tp->srtt_us))
		bbr_init_pacing_rate_from_rtt(sk);
	if (bbr_full_bw_reached(sk) || rate > READ_ONCE(sk->sk_pacing_rate))
		WRITE_ONCE(sk->sk_pacing_rate, rate);
}

static u32 bbr_min_tso_segs(struct sock *sk)
{
	return READ_ONCE(sk->sk_pacing_rate) < (bbr_min_tso_rate >> 3) ? 1 : 2;
}

static u32 bbr_tso_segs_goal(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 segs, r;
	u64 bytes;

	bytes = READ_ONCE(sk->sk_pacing_rate) >> READ_ONCE(sk->sk_pacing_shift);
	if (bbr_tso_rtt_shift) {
		r = bbr->min_rtt_us >> bbr_tso_rtt_shift;
		if (r < BITS_PER_TYPE(u32))
			bytes += GSO_MAX_SIZE >> r;
	}
	bytes = min_t(u64, bytes, GSO_MAX_SIZE - 1 - MAX_TCP_HEADER);
	segs = max_t(u32, div_u64(bytes, max_t(u32, tp->mss_cache, 1U)),
		     bbr_min_tso_segs(sk));
	return min(segs, 0x7FU);
}

static void bbr_save_cwnd(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr->prev_ca_state < TCP_CA_Recovery && bbr->mode != BBR_PROBE_RTT)
		bbr->prior_cwnd = tp->snd_cwnd;
	else
		bbr->prior_cwnd = max(bbr->prior_cwnd, tp->snd_cwnd);
}

static void bbr_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	if (event == CA_EVENT_TX_START && tp->app_limited) {
		bbr->idle_restart = 1;
		bbr->ack_epoch_mstamp = tp->tcp_mstamp;
		bbr->ack_epoch_acked = 0;
		if (bbr->mode == BBR_PROBE_BW)
			bbr_set_pacing_rate(sk, bbr_bw(sk), BBR_UNIT);
		else if (bbr->mode == BBR_PROBE_RTT)
			bbr_check_probe_rtt_done(sk);
	}
}

static u32 bbr_bdp(struct sock *sk, u32 bw, int gain)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 bdp;
	u64 w;

	if (unlikely(bbr->min_rtt_us == ~0U))
		return bbr->init_cwnd ? bbr->init_cwnd : TCP_INIT_CWND;

	w = (u64)bw * bbr->min_rtt_us;
	bdp = (((w * gain) >> BBR_SCALE) + BW_UNIT - 1) / BW_UNIT;
	return bdp;
}

static u32 bbr_quantization_budget(struct sock *sk, u32 cwnd)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 tso_segs_goal = 3 * bbr_tso_segs_goal(sk);

	cwnd = max_t(u32, cwnd, tso_segs_goal);
	cwnd = max_t(u32, cwnd, bbr_cwnd_min_target);
	if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == BBR_BW_PROBE_UP)
		cwnd += 2;
	return cwnd;
}

static u32 bbr_inflight(struct sock *sk, u32 bw, int gain)
{
	u32 inflight;

	inflight = bbr_bdp(sk, bw, gain);
	inflight = bbr_quantization_budget(sk, inflight);
	return inflight;
}

static u32 bbr_packets_in_net_at_edt(struct sock *sk, u32 inflight_now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u64 now_ns, edt_ns, interval_us;
	u32 interval_delivered, inflight_at_edt;

	now_ns = tp->tcp_clock_cache;
	edt_ns = max(tp->tcp_wstamp_ns, now_ns);
	interval_us = div_u64(edt_ns - now_ns, NSEC_PER_USEC);
	interval_delivered = (u64)bbr_bw(sk) * interval_us >> BW_SCALE;
	inflight_at_edt = inflight_now;
	if (bbr->pacing_gain > BBR_UNIT)
		inflight_at_edt += bbr_tso_segs_goal(sk);
	if (interval_delivered >= inflight_at_edt)
		return 0;
	return inflight_at_edt - interval_delivered;
}

static u32 bbr_ack_aggregation_cwnd(struct sock *sk)
{
	u32 max_aggr_cwnd, aggr_cwnd = 0;

	if (bbr_extra_acked_gain) {
		max_aggr_cwnd = ((u64)bbr_bw(sk) * bbr_extra_acked_max_us)
				/ BW_UNIT;
		aggr_cwnd = (bbr_extra_acked_gain * bbr_extra_acked(sk))
			     >> BBR_SCALE;
		aggr_cwnd = min(aggr_cwnd, max_aggr_cwnd);
	}
	return aggr_cwnd;
}

static u32 bbr_probe_rtt_cwnd(struct sock *sk)
{
	return max_t(u32, bbr_cwnd_min_target,
		     bbr_bdp(sk, bbr_bw(sk), bbr_probe_rtt_cwnd_gain));
}

static void bbr_set_cwnd(struct sock *sk, const struct rate_sample *rs,
			 u32 acked, u32 bw, int gain, u32 cwnd)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 target_cwnd = 0;

	if (!acked)
		goto done;

	target_cwnd = bbr_bdp(sk, bw, gain);
	target_cwnd += bbr_ack_aggregation_cwnd(sk);
	target_cwnd = bbr_quantization_budget(sk, target_cwnd);

	bbr->try_fast_path = 0;
	if (bbr_full_bw_reached(sk)) {
		cwnd += acked;
		if (cwnd >= target_cwnd) {
			cwnd = target_cwnd;
			bbr->try_fast_path = 1;
		}
	} else if (cwnd < target_cwnd || cwnd < 2U * bbr->init_cwnd) {
		cwnd += acked;
	} else {
		bbr->try_fast_path = 1;
	}

	cwnd = max_t(u32, cwnd, bbr_cwnd_min_target);
done:
	tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);
	if (bbr->mode == BBR_PROBE_RTT)
		tp->snd_cwnd = min_t(u32, tp->snd_cwnd, bbr_probe_rtt_cwnd(sk));
}

static void bbr_reset_startup_mode(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->mode = BBR_STARTUP;
}

static u32 bbr_update_round_start(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 round_delivered = 0;

	bbr->round_start = 0;
	if (rs->interval_us > 0 &&
	    !before(rs->prior_delivered, bbr->next_rtt_delivered)) {
		round_delivered = tp->delivered - bbr->next_rtt_delivered;
		bbr->next_rtt_delivered = tp->delivered;
		bbr->round_start = 1;
	}
	return round_delivered;
}

static void bbr_calculate_bw_sample(const struct rate_sample *rs,
				    struct bbr_context *ctx)
{
	u64 bw = 0;

	if (rs->interval_us > 0) {
		if (WARN_ONCE(rs->delivered < 0,
			      "negative delivered: %d interval_us: %ld\n",
			      rs->delivered, rs->interval_us))
			return;
		bw = DIV_ROUND_UP_ULL((u64)rs->delivered * BW_UNIT,
				      rs->interval_us);
	}
	ctx->sample_bw = bw;
}

static void bbr_update_ack_aggregation(struct sock *sk,
				       const struct rate_sample *rs)
{
	u32 epoch_us, expected_acked, extra_acked;
	struct bbr *bbr = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 extra_acked_win_rtts_thresh = bbr_extra_acked_win_rtts;

	if (!bbr_extra_acked_gain || rs->acked_sacked <= 0 ||
	    rs->delivered < 0 || rs->interval_us <= 0)
		return;

	if (bbr->round_start) {
		bbr->extra_acked_win_rtts = min(0x1FU,
						bbr->extra_acked_win_rtts + 1U);
		if (!bbr_full_bw_reached(sk))
			extra_acked_win_rtts_thresh = 1;
		if (bbr->extra_acked_win_rtts >= extra_acked_win_rtts_thresh) {
			bbr->extra_acked_win_rtts = 0;
			bbr->extra_acked_win_idx = bbr->extra_acked_win_idx ?
						   0 : 1;
			bbr->extra_acked[bbr->extra_acked_win_idx] = 0;
		}
	}

	epoch_us = tcp_stamp_us_delta(tp->delivered_mstamp,
				      bbr->ack_epoch_mstamp);
	expected_acked = ((u64)bbr_bw(sk) * epoch_us) / BW_UNIT;

	if (bbr->ack_epoch_acked <= expected_acked ||
	    (bbr->ack_epoch_acked + rs->acked_sacked >=
	     bbr_ack_epoch_acked_reset_thresh)) {
		bbr->ack_epoch_acked = 0;
		bbr->ack_epoch_mstamp = tp->delivered_mstamp;
		expected_acked = 0;
	}

	bbr->ack_epoch_acked = min_t(u32, 0xFFFFFU,
				     bbr->ack_epoch_acked + rs->acked_sacked);
	extra_acked = bbr->ack_epoch_acked - expected_acked;
	extra_acked = min(extra_acked, tp->snd_cwnd);
	if (extra_acked > bbr->extra_acked[bbr->extra_acked_win_idx])
		bbr->extra_acked[bbr->extra_acked_win_idx] = extra_acked;
}

static void bbr_check_probe_rtt_done(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	if (!(bbr->probe_rtt_done_stamp &&
	      after(tcp_jiffies32, bbr->probe_rtt_done_stamp)))
		return;

	bbr->probe_rtt_min_stamp = tcp_jiffies32;
	tp->snd_cwnd = max(tp->snd_cwnd, bbr->prior_cwnd);
	bbr_exit_probe_rtt(sk);
}

static void bbr_update_min_rtt(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	bool probe_rtt_expired, min_rtt_expired;
	u32 expire;

	expire = bbr->probe_rtt_min_stamp +
		 msecs_to_jiffies(bbr_probe_rtt_win_ms);
	probe_rtt_expired = after(tcp_jiffies32, expire);
	if (rs->rtt_us >= 0 &&
	    (rs->rtt_us < bbr->probe_rtt_min_us ||
	     (probe_rtt_expired && !rs->is_ack_delayed))) {
		bbr->probe_rtt_min_us = rs->rtt_us;
		bbr->probe_rtt_min_stamp = tcp_jiffies32;
	}
	expire = bbr->min_rtt_stamp + bbr_min_rtt_win_sec * HZ;
	min_rtt_expired = after(tcp_jiffies32, expire);
	if (bbr->probe_rtt_min_us <= bbr->min_rtt_us || min_rtt_expired) {
		bbr->min_rtt_us = bbr->probe_rtt_min_us;
		bbr->min_rtt_stamp = bbr->probe_rtt_min_stamp;
	}

	if (bbr_probe_rtt_mode_ms > 0 && probe_rtt_expired &&
	    !bbr->idle_restart && bbr->mode != BBR_PROBE_RTT) {
		bbr->mode = BBR_PROBE_RTT;
		bbr_save_cwnd(sk);
		bbr->probe_rtt_done_stamp = 0;
		bbr->ack_phase = BBR_ACKS_PROBE_STOPPING;
		bbr->next_rtt_delivered = tp->delivered;
	}

	if (bbr->mode == BBR_PROBE_RTT) {
		tp->app_limited =
			(tp->delivered + tcp_packets_in_flight(tp)) ? : 1;
		if (!bbr->probe_rtt_done_stamp &&
		    tcp_packets_in_flight(tp) <= bbr_probe_rtt_cwnd(sk)) {
			bbr->probe_rtt_done_stamp = tcp_jiffies32 +
				msecs_to_jiffies(bbr_probe_rtt_mode_ms);
			bbr->probe_rtt_round_done = 0;
			bbr->next_rtt_delivered = tp->delivered;
		} else if (bbr->probe_rtt_done_stamp) {
			if (bbr->round_start)
				bbr->probe_rtt_round_done = 1;
			if (bbr->probe_rtt_round_done)
				bbr_check_probe_rtt_done(sk);
		}
	}
	if (rs->delivered > 0)
		bbr->idle_restart = 0;
}

static void bbr_update_gains(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	switch (bbr->mode) {
	case BBR_STARTUP:
		bbr->pacing_gain = bbr_startup_pacing_gain;
		bbr->cwnd_gain   = bbr_startup_cwnd_gain;
		break;
	case BBR_DRAIN:
		bbr->pacing_gain = bbr_drain_gain;
		bbr->cwnd_gain   = bbr_startup_cwnd_gain;
		break;
	case BBR_PROBE_BW:
		bbr->pacing_gain = bbr_pacing_gain[bbr->cycle_idx];
		bbr->cwnd_gain   = bbr_cwnd_gain;
		if (bbr_bw_probe_cwnd_gain &&
		    bbr->cycle_idx == BBR_BW_PROBE_UP)
			bbr->cwnd_gain += BBR_UNIT * bbr_bw_probe_cwnd_gain / 4;
		break;
	case BBR_PROBE_RTT:
		bbr->pacing_gain = BBR_UNIT;
		bbr->cwnd_gain   = BBR_UNIT;
		break;
	default:
		WARN_ONCE(1, "BBR bad mode: %u\n", bbr->mode);
		break;
	}
}

static u32 bbr_sndbuf_expand(struct sock *sk)
{
	return 3;
}

static void bbr_take_max_bw_sample(struct sock *sk, u32 bw)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->bw_hi[1] = max(bw, bbr->bw_hi[1]);
}

static void bbr_advance_max_bw_filter(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (!bbr->bw_hi[1])
		return;
	bbr->bw_hi[0] = bbr->bw_hi[1];
	bbr->bw_hi[1] = 0;
}

static void bbr_reset_full_bw(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->full_bw = 0;
	bbr->full_bw_cnt = 0;
	bbr->full_bw_now = 0;
}

static u32 bbr_target_inflight(struct sock *sk)
{
	u32 bdp = bbr_inflight(sk, bbr_bw(sk), BBR_UNIT);

	return min(bdp, tcp_sk(sk)->snd_cwnd);
}

static bool bbr_is_probing_bandwidth(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	return (bbr->mode == BBR_STARTUP) ||
		(bbr->mode == BBR_PROBE_BW &&
		 (bbr->cycle_idx == BBR_BW_PROBE_REFILL ||
		  bbr->cycle_idx == BBR_BW_PROBE_UP));
}

static void bbr_handle_queue_too_high_in_startup(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 bdp;

	bbr->full_bw_reached = 1;
	bdp = bbr_inflight(sk, bbr_max_bw(sk), BBR_UNIT);
	bbr->inflight_hi = max(bdp, bbr->inflight_latest);
}

static void bbr_raise_inflight_hi_slope(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 growth_this_round, cnt;

	growth_this_round = 1U << bbr->bw_probe_up_rounds;
	bbr->bw_probe_up_rounds = min(bbr->bw_probe_up_rounds + 1U, 30U);
	cnt = tp->snd_cwnd / growth_this_round;
	cnt = clamp(cnt, 1U, 0xFFFEU);
	bbr->bw_probe_up_cnt = (u16)cnt;
}

static void bbr_probe_inflight_hi_upward(struct sock *sk,
					 const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 delta, total_acks;

	if (!tp->is_cwnd_limited || tp->snd_cwnd < bbr->inflight_hi ||
	    !bbr->bw_probe_up_cnt || bbr->bw_probe_up_cnt == 0xFFFFU)
		return;

	total_acks = (u32)bbr->bw_probe_up_acks + rs->acked_sacked;
	if (total_acks >= bbr->bw_probe_up_cnt) {
		delta = total_acks / bbr->bw_probe_up_cnt;
		total_acks -= delta * bbr->bw_probe_up_cnt;
		bbr->inflight_hi += delta;
		bbr->try_fast_path = 0;
	}
	bbr->bw_probe_up_acks = (u16)total_acks;

	if (bbr->round_start)
		bbr_raise_inflight_hi_slope(sk);
}

static bool bbr_is_inflight_too_high(const struct rate_sample *rs)
{
	u32 loss_thresh;

	if (rs->losses > 0 && rs->prior_in_flight) {
		loss_thresh = (u64)rs->prior_in_flight * bbr_loss_thresh >>
				BBR_SCALE;
		if ((u32)rs->losses > loss_thresh)
			return true;
	}
	return false;
}

static u32 bbr_inflight_hi_from_sample(const struct rate_sample *rs)
{
	u32 safe_inflight, loss_budget;

	if ((u32)rs->losses >= rs->prior_in_flight)
		return bbr_cwnd_min_target;

	safe_inflight = rs->prior_in_flight - (u32)rs->losses;
	loss_budget = ((u64)safe_inflight * bbr_loss_thresh + BBR_UNIT - 1) >>
		      BBR_SCALE;
	return safe_inflight + loss_budget;
}

static u32 bbr_inflight_with_headroom(const struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 headroom;

	if (bbr->inflight_hi == ~0U)
		return ~0U;

	headroom = ((u64)bbr->inflight_hi * bbr_inflight_headroom) >> BBR_SCALE;
	headroom = max(headroom, 1U);
	return max_t(s32, bbr->inflight_hi - headroom, bbr_cwnd_min_target);
}

static void bbr_bound_cwnd_for_inflight_model(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	u32 cap;

	if (!bbr->initialized)
		return;

	cap = ~0U;
	if (bbr->mode == BBR_PROBE_BW &&
	    bbr->cycle_idx != BBR_BW_PROBE_CRUISE) {
		cap = bbr->inflight_hi;
	} else if (bbr->mode == BBR_PROBE_RTT ||
		   (bbr->mode == BBR_PROBE_BW &&
		    bbr->cycle_idx == BBR_BW_PROBE_CRUISE)) {
		cap = bbr_inflight_with_headroom(sk);
	}
	cap = min(cap, bbr->inflight_lo);
	cap = max_t(u32, cap, bbr_cwnd_min_target);
	tp->snd_cwnd = min(cap, tp->snd_cwnd);
}

static void bbr_init_lower_bounds(struct sock *sk, bool init_bw)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	if (init_bw && bbr->bw_lo == ~0U)
		bbr->bw_lo = bbr_max_bw(sk);
	if (bbr->inflight_lo == ~0U)
		bbr->inflight_lo = tp->snd_cwnd;
}

static void bbr_loss_lower_bounds(struct sock *sk, u32 *bw, u32 *inflight)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 loss_cut = BBR_UNIT - bbr_beta;

	*bw = max_t(u32, bbr->bw_latest,
		    (u64)bbr->bw_lo * loss_cut >> BBR_SCALE);
	*inflight = max_t(u32, bbr->inflight_latest,
			  (u64)bbr->inflight_lo * loss_cut >> BBR_SCALE);
}

static void bbr_adapt_lower_bounds(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr_is_probing_bandwidth(sk))
		return;

	if (bbr->loss_in_round) {
		bbr_init_lower_bounds(sk, true);
		bbr_loss_lower_bounds(sk, &bbr->bw_lo, &bbr->inflight_lo);
	}
	bbr->bw_lo = max(1U, bbr->bw_lo);
}

static void bbr_reset_lower_bounds(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->bw_lo = ~0U;
	bbr->inflight_lo = ~0U;
}

static void bbr_reset_congestion_signals(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->loss_in_round = 0;
	bbr->loss_in_cycle = 0;
	bbr->bw_latest = 0;
	bbr->inflight_latest = 0;
}

static void bbr_exit_loss_recovery(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	tp->snd_cwnd = max(tp->snd_cwnd, bbr->prior_cwnd);
	bbr->try_fast_path = 0;
}

static void bbr_update_latest_delivery_signals(struct sock *sk,
					       const struct rate_sample *rs,
					       struct bbr_context *ctx)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->loss_round_start = 0;
	if (rs->interval_us <= 0 || !rs->acked_sacked)
		return;

	bbr->bw_latest       = max_t(u32, bbr->bw_latest, ctx->sample_bw);
	bbr->inflight_latest = max_t(u32, bbr->inflight_latest, rs->delivered);

	if (!before(rs->prior_delivered, bbr->loss_round_delivered)) {
		bbr->loss_round_delivered = tp->delivered;
		bbr->loss_round_start = 1;
	}
}

static void bbr_advance_latest_delivery_signals(struct sock *sk,
						const struct rate_sample *rs,
						struct bbr_context *ctx)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr->loss_round_start && !rs->is_retrans) {
		bbr->bw_latest = ctx->sample_bw;
		bbr->inflight_latest = max_t(s32, rs->delivered, 0);
	}
}

static void bbr_update_congestion_signals(struct sock *sk,
					  const struct rate_sample *rs,
					  struct bbr_context *ctx)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u64 bw;

	if (rs->interval_us <= 0 || !rs->acked_sacked)
		return;
	bw = ctx->sample_bw;

	if (!rs->is_app_limited || bw >= bbr_max_bw(sk))
		bbr_take_max_bw_sample(sk, bw);

	bbr->loss_in_round |= (rs->losses > 0);

	if (!bbr->loss_round_start)
		return;
	bbr_adapt_lower_bounds(sk);
	bbr->loss_in_round = 0;
}

static bool bbr_is_reno_coexistence_probe_time(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 rounds;

	rounds = min_t(u32, bbr_bw_probe_max_rounds, bbr_target_inflight(sk));
	return bbr->rounds_since_probe >= rounds;
}

static void bbr_pick_probe_wait(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 wait_us;

	bbr->rounds_since_probe = prandom_u32_max(bbr_bw_probe_rand_rounds);
	wait_us = bbr_bw_probe_base_us + prandom_u32_max(bbr_bw_probe_rand_us);
	bbr->probe_deadline_stamp = tcp_jiffies32 + usecs_to_jiffies(wait_us);
}

static void bbr_set_cycle_idx(struct sock *sk, int cycle_idx)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->cycle_idx = cycle_idx;
	bbr->try_fast_path = 0;
}

static void bbr_start_bw_probe_refill(struct sock *sk, u32 bw_probe_up_rounds)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	bbr_reset_lower_bounds(sk);
	bbr->bw_probe_up_rounds = bw_probe_up_rounds;
	bbr->bw_probe_up_acks = 0;
	bbr->stopped_risky_probe = 0;
	bbr->ack_phase = BBR_ACKS_REFILLING;
	bbr->next_rtt_delivered = tp->delivered;
	bbr_set_cycle_idx(sk, BBR_BW_PROBE_REFILL);
}

static void bbr_start_bw_probe_up(struct sock *sk, struct bbr_context *ctx)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->ack_phase = BBR_ACKS_PROBE_STARTING;
	bbr->next_rtt_delivered = tp->delivered;
	bbr_reset_full_bw(sk);
	bbr->full_bw = ctx->sample_bw;
	bbr_set_cycle_idx(sk, BBR_BW_PROBE_UP);
	bbr_raise_inflight_hi_slope(sk);
}

static void bbr_start_bw_probe_down(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	bbr_reset_congestion_signals(sk);
	bbr->bw_probe_up_cnt = 0xFFFFU;
	bbr_pick_probe_wait(sk);
	bbr->ack_phase = BBR_ACKS_PROBE_STOPPING;
	bbr->next_rtt_delivered = tp->delivered;
	bbr_set_cycle_idx(sk, BBR_BW_PROBE_DOWN);
}

static void bbr_start_bw_probe_cruise(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr->inflight_lo != ~0U)
		bbr->inflight_lo = min(bbr->inflight_lo, bbr->inflight_hi);
	bbr_set_cycle_idx(sk, BBR_BW_PROBE_CRUISE);
}

static void bbr_handle_inflight_too_high(struct sock *sk,
					 const struct rate_sample *rs)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 sample_hi;

	bbr->prev_probe_too_high = 1;
	bbr->bw_probe_samples = 0;
	if (!rs->is_app_limited) {
		sample_hi = bbr_inflight_hi_from_sample(rs);
		bbr->inflight_hi = max_t(u32, sample_hi,
					 (u64)bbr_target_inflight(sk) *
					 (BBR_UNIT - bbr_beta) >> BBR_SCALE);
	}
	if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == BBR_BW_PROBE_UP)
		bbr_start_bw_probe_down(sk);
}

static bool bbr_adapt_upper_bounds(struct sock *sk,
				   const struct rate_sample *rs)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr->ack_phase == BBR_ACKS_PROBE_STARTING && bbr->round_start)
		bbr->ack_phase = BBR_ACKS_PROBE_FEEDBACK;
	if (bbr->ack_phase == BBR_ACKS_PROBE_STOPPING && bbr->round_start) {
		bbr->bw_probe_samples = 0;
		bbr->ack_phase = BBR_ACKS_INIT;
		if (bbr->mode == BBR_PROBE_BW && !rs->is_app_limited)
			bbr_advance_max_bw_filter(sk);
		if (bbr->mode == BBR_PROBE_BW &&
		    bbr->stopped_risky_probe && !bbr->prev_probe_too_high) {
			bbr_start_bw_probe_refill(sk, 0);
			return true;
		}
	}
	if (bbr_is_inflight_too_high(rs)) {
		if (bbr->bw_probe_samples)
			bbr_handle_inflight_too_high(sk, rs);
	} else {
		if (bbr->inflight_hi == ~0U)
			return false;
		if (rs->prior_in_flight > bbr->inflight_hi)
			bbr->inflight_hi = rs->prior_in_flight;
		if (bbr->mode == BBR_PROBE_BW &&
		    bbr->cycle_idx == BBR_BW_PROBE_UP)
			bbr_probe_inflight_hi_upward(sk, rs);
	}
	return false;
}

static bool bbr_check_time_to_probe_bw(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (after(tcp_jiffies32, bbr->probe_deadline_stamp) ||
	    bbr_is_reno_coexistence_probe_time(sk)) {
		bbr_start_bw_probe_refill(sk, 0);
		return true;
	}
	return false;
}

static bool bbr_check_time_to_cruise(struct sock *sk, u32 inflight, u32 bw)
{
	if (inflight > bbr_inflight_with_headroom(sk))
		return false;
	return inflight <= bbr_inflight(sk, bw, BBR_UNIT);
}

static void bbr_update_cycle_phase(struct sock *sk,
				   const struct rate_sample *rs,
				   struct bbr_context *ctx)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	bool is_bw_probe_done = false;
	u32 inflight, bw;

	if (!bbr_full_bw_reached(sk))
		return;

	if (bbr_adapt_upper_bounds(sk, rs))
		return;

	if (bbr->mode != BBR_PROBE_BW)
		return;

	inflight = bbr_packets_in_net_at_edt(sk, rs->prior_in_flight);
	bw = bbr_max_bw(sk);

	switch (bbr->cycle_idx) {
	case BBR_BW_PROBE_CRUISE:
		if (bbr_check_time_to_probe_bw(sk))
			return;
		break;
	case BBR_BW_PROBE_REFILL:
		if (bbr->round_start) {
			bbr->bw_probe_samples = 1;
			bbr_start_bw_probe_up(sk, ctx);
		}
		break;
	case BBR_BW_PROBE_UP:
		if (bbr->prev_probe_too_high && inflight >= bbr->inflight_hi) {
			bbr->stopped_risky_probe = 1;
			is_bw_probe_done = true;
		} else {
			if (tp->is_cwnd_limited &&
			    tp->snd_cwnd >= bbr->inflight_hi) {
				bbr_reset_full_bw(sk);
				bbr->full_bw = ctx->sample_bw;
			} else if (bbr->full_bw_now) {
				is_bw_probe_done = true;
			}
		}
		if (is_bw_probe_done) {
			bbr->prev_probe_too_high = 0;
			bbr_start_bw_probe_down(sk);
		}
		break;
	case BBR_BW_PROBE_DOWN:
		if (bbr_check_time_to_probe_bw(sk))
			return;
		if (bbr_check_time_to_cruise(sk, inflight, bw))
			bbr_start_bw_probe_cruise(sk);
		break;
	default:
		WARN_ONCE(1, "BBR invalid cycle index %u\n", bbr->cycle_idx);
	}
}

static void bbr_exit_probe_rtt(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr_reset_lower_bounds(sk);
	if (bbr_full_bw_reached(sk)) {
		bbr->mode = BBR_PROBE_BW;
		bbr_start_bw_probe_down(sk);
		bbr_start_bw_probe_cruise(sk);
	} else {
		bbr->mode = BBR_STARTUP;
	}
}

static void bbr_check_loss_too_high_in_startup(struct sock *sk,
					       const struct rate_sample *rs)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr_full_bw_reached(sk))
		return;

	if (rs->losses > 0 && bbr->loss_events_in_round < 0xFU)
		bbr->loss_events_in_round++;
	if (bbr_full_loss_cnt && bbr->loss_round_start &&
	    inet_csk(sk)->icsk_ca_state == TCP_CA_Recovery &&
	    bbr->loss_events_in_round >= bbr_full_loss_cnt &&
	    bbr_is_inflight_too_high(rs)) {
		bbr_handle_queue_too_high_in_startup(sk);
		return;
	}
	if (bbr->loss_round_start)
		bbr->loss_events_in_round = 0;
}

static void bbr_check_full_bw_reached(struct sock *sk,
				      const struct rate_sample *rs,
				      struct bbr_context *ctx)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 bw_thresh;

	if (bbr->full_bw_now || rs->is_app_limited)
		return;

	bw_thresh = (u64)bbr->full_bw * bbr_full_bw_thresh >> BBR_SCALE;
	if (ctx->sample_bw >= bw_thresh) {
		bbr_reset_full_bw(sk);
		bbr->full_bw = ctx->sample_bw;
		return;
	}
	if (!bbr->round_start)
		return;
	++bbr->full_bw_cnt;
	bbr->full_bw_now = bbr->full_bw_cnt >= bbr_full_bw_cnt;
	bbr->full_bw_reached |= bbr->full_bw_now;
}

static void bbr_check_drain(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	if (bbr->mode == BBR_STARTUP && bbr_full_bw_reached(sk)) {
		bbr->mode = BBR_DRAIN;
		tcp_sk(sk)->snd_ssthresh =
			bbr_inflight(sk, bbr_max_bw(sk), BBR_UNIT);
		bbr_reset_congestion_signals(sk);
	}
	if (bbr->mode == BBR_DRAIN &&
	    bbr_packets_in_net_at_edt(sk, tcp_packets_in_flight(tcp_sk(sk))) <=
	    bbr_inflight(sk, bbr_max_bw(sk), BBR_UNIT)) {
		bbr->mode = BBR_PROBE_BW;
		bbr_start_bw_probe_down(sk);
	}
}

static void bbr_update_model(struct sock *sk, const struct rate_sample *rs,
			     struct bbr_context *ctx)
{
	bbr_update_congestion_signals(sk, rs, ctx);
	bbr_update_ack_aggregation(sk, rs);
	bbr_check_loss_too_high_in_startup(sk, rs);
	bbr_check_full_bw_reached(sk, rs, ctx);
	bbr_check_drain(sk);
	bbr_update_cycle_phase(sk, rs, ctx);
	bbr_update_min_rtt(sk, rs);
}

static bool bbr_run_fast_path(struct sock *sk, bool *update_model,
			      const struct rate_sample *rs,
			      struct bbr_context *ctx)
{
	struct bbr *bbr = inet_csk_ca(sk);
	u32 prev_min_rtt_us, prev_mode;

	if (bbr_fast_path && bbr->try_fast_path &&
	    rs->is_app_limited && ctx->sample_bw < bbr_max_bw(sk) &&
	    !bbr->loss_in_round) {
		prev_mode = bbr->mode;
		prev_min_rtt_us = bbr->min_rtt_us;
		bbr_check_drain(sk);
		bbr_update_cycle_phase(sk, rs, ctx);
		bbr_update_min_rtt(sk, rs);

		if (bbr->mode == prev_mode &&
		    bbr->min_rtt_us == prev_min_rtt_us &&
		    bbr->try_fast_path)
			return true;

		*update_model = false;
	}
	return false;
}

static void bbr_main(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);
	struct bbr_context ctx = { 0 };
	bool update_model = true;
	u32 bw, round_delivered;

	round_delivered = bbr_update_round_start(sk, rs);
	if (bbr->round_start)
		bbr->rounds_since_probe =
			min_t(s32, bbr->rounds_since_probe + 1, 0xFF);

	bbr_calculate_bw_sample(rs, &ctx);
	bbr_update_latest_delivery_signals(sk, rs, &ctx);

	if (bbr_run_fast_path(sk, &update_model, rs, &ctx))
		goto out;

	if (update_model)
		bbr_update_model(sk, rs, &ctx);

	bbr_update_gains(sk);
	bw = bbr_bw(sk);
	bbr_set_pacing_rate(sk, bw, bbr->pacing_gain);
	bbr_set_cwnd(sk, rs, rs->acked_sacked, bw, bbr->cwnd_gain,
		     tp->snd_cwnd);
	bbr_bound_cwnd_for_inflight_model(sk);

out:
	bbr_advance_latest_delivery_signals(sk, rs, &ctx);
	bbr->prev_ca_state = inet_csk(sk)->icsk_ca_state;
	bbr->loss_in_cycle |= (rs->losses > 0);
	(void)round_delivered;
}

static void bbr_init(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	bbr->initialized = 1;
	bbr->init_cwnd = min(0x7FU, tp->snd_cwnd);
	bbr->prior_cwnd = tp->prior_cwnd;
	tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;
	bbr->next_rtt_delivered = tp->delivered;
	bbr->prev_ca_state = TCP_CA_Open;

	bbr->probe_rtt_done_stamp = 0;
	bbr->probe_rtt_round_done = 0;
	bbr->probe_rtt_min_us = tcp_min_rtt(tp);
	bbr->probe_rtt_min_stamp = tcp_jiffies32;
	bbr->min_rtt_us = tcp_min_rtt(tp);
	bbr->min_rtt_stamp = tcp_jiffies32;

	bbr->has_seen_rtt = 0;
	bbr_init_pacing_rate_from_rtt(sk);

	bbr->round_start = 0;
	bbr->idle_restart = 0;
	bbr->full_bw_reached = 0;
	bbr->full_bw = 0;
	bbr->full_bw_cnt = 0;
	bbr->full_bw_now = 0;
	bbr->cycle_idx = 0;
	bbr_reset_startup_mode(sk);

	bbr->ack_epoch_mstamp = tp->tcp_mstamp;
	bbr->ack_epoch_acked = 0;
	bbr->extra_acked_win_rtts = 0;
	bbr->extra_acked_win_idx = 0;
	bbr->extra_acked[0] = 0;
	bbr->extra_acked[1] = 0;

	bbr->bw_lo = ~0U;
	bbr->bw_hi[0] = 0;
	bbr->bw_hi[1] = 0;
	bbr->inflight_lo = ~0U;
	bbr->inflight_hi = ~0U;
	bbr->try_fast_path = 0;
	bbr->rounds_since_probe = 0;
	bbr->bw_probe_up_rounds = 0;
	bbr->bw_probe_up_cnt = 0xFFFFU;
	bbr->bw_probe_up_acks = 0;
	bbr->bw_probe_samples = 0;
	bbr->prev_probe_too_high = 0;
	bbr->stopped_risky_probe = 0;
	bbr->ack_phase = BBR_ACKS_INIT;
	bbr->loss_events_in_round = 0;
	bbr->loss_round_delivered = tp->delivered;
	bbr->loss_round_start = 0;
	bbr->undo_bw_lo = 0;
	bbr->undo_inflight_lo = 0;
	bbr->undo_inflight_hi = 0;
	bbr_reset_congestion_signals(sk);

	cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

static u32 bbr_undo_cwnd(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr_reset_full_bw(sk);
	bbr->loss_in_round = 0;
	bbr->bw_lo = max(bbr->bw_lo, bbr->undo_bw_lo);
	bbr->inflight_lo = max(bbr->inflight_lo, bbr->undo_inflight_lo);
	bbr->inflight_hi = max(bbr->inflight_hi, bbr->undo_inflight_hi);
	bbr->try_fast_path = 0;
	return bbr->prior_cwnd;
}

static u32 bbr_ssthresh(struct sock *sk)
{
	struct bbr *bbr = inet_csk_ca(sk);

	bbr_save_cwnd(sk);
	bbr->undo_bw_lo = bbr->bw_lo;
	bbr->undo_inflight_lo = bbr->inflight_lo;
	bbr->undo_inflight_hi = bbr->inflight_hi;
	return tcp_sk(sk)->snd_ssthresh;
}

static size_t bbr_get_info(struct sock *sk, u32 ext, int *attr,
			   union tcp_cc_info *info)
{
	if (ext & (1 << (INET_DIAG_BBRINFO - 1)) ||
	    ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		struct bbr *bbr = inet_csk_ca(sk);
		u64 bw = bbr_bw_bytes_per_sec(sk, bbr_bw(sk));

		memset(&info->bbr, 0, sizeof(info->bbr));
		info->bbr.bbr_bw_lo       = (u32)bw;
		info->bbr.bbr_bw_hi       = (u32)(bw >> 32);
		info->bbr.bbr_min_rtt     = bbr->min_rtt_us;
		info->bbr.bbr_pacing_gain = bbr->pacing_gain;
		info->bbr.bbr_cwnd_gain   = bbr->cwnd_gain;
		*attr = INET_DIAG_BBRINFO;
		return sizeof(info->bbr);
	}
	return 0;
}

static void bbr_set_state(struct sock *sk, u8 new_state)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bbr *bbr = inet_csk_ca(sk);

	if (new_state == TCP_CA_Loss) {
		bbr->prev_ca_state = TCP_CA_Loss;
		bbr_reset_full_bw(sk);
		if (!bbr_is_probing_bandwidth(sk) && bbr->inflight_lo == ~0U)
			bbr->inflight_lo = max(tp->snd_cwnd, bbr->prior_cwnd);
	} else if (bbr->prev_ca_state == TCP_CA_Loss &&
		   new_state != TCP_CA_Loss) {
		bbr_exit_loss_recovery(sk);
	}
}

static struct tcp_congestion_ops tcp_bbr_cong_ops __read_mostly = {
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "bbr",
	.owner		= THIS_MODULE,
	.init		= bbr_init,
	.cong_control	= bbr_main,
	.sndbuf_expand	= bbr_sndbuf_expand,
	.undo_cwnd	= bbr_undo_cwnd,
	.cwnd_event	= bbr_cwnd_event,
	.ssthresh	= bbr_ssthresh,
	.min_tso_segs	= bbr_min_tso_segs,
	.get_info	= bbr_get_info,
	.set_state	= bbr_set_state,
};

static int __init bbr_register(void)
{
	BUILD_BUG_ON(sizeof(struct bbr) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&tcp_bbr_cong_ops);
}

static void __exit bbr_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_bbr_cong_ops);
}

module_init(bbr_register);
module_exit(bbr_unregister);

MODULE_AUTHOR("Van Jacobson <vanj@google.com>");
MODULE_AUTHOR("Neal Cardwell <ncardwell@google.com>");
MODULE_AUTHOR("Yuchung Cheng <ycheng@google.com>");
MODULE_AUTHOR("Soheil Hassas Yeganeh <soheil@google.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("TCP BBRv3 (Bottleneck Bandwidth and RTT)");
