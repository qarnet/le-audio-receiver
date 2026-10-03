/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/i2s.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio_sink.h"
#include "audio_drift.h"
#include "audio_offload.h"
#include "clock_i2s.h"

extern int peer_fault;
extern bool correction_disabled;
extern int32_t applied_ppm;
extern unsigned observed_underruns;
extern bool peer_fail_after_fault, peer_fail_submissions;
#define ONE (1ULL << 32)

enum violation {
	OK,
	WAVEFORM,
	REPEAT,
	SOURCE_LATE,
	QUEUE,
	RATE,
	CLOCK,
	PUSH,
	RECOVERY
};
enum stimulus {
	JITTER = 1,
	LOSS = 2,
	DELAY = 4,
	STEP = 8,
	FAULTS = 16
};
struct reference {
	uint64_t coordinate, available, loss_boundary;
	unsigned frames;
	unsigned write_index;
	bool startup, wave_error, cadence_error;
};
struct scenario {
	const char *name;
	unsigned frames;
	int pclk, source_ppm;
	unsigned stimuli;
	bool disabled;
	bool fail_remote_after_fault;
};

static uint64_t nominal(void)
{
	return (2ULL * 48000 * ONE + 47619) / (2ULL * 47619);
}
static uint64_t step_for(int32_t ppm)
{
	uint64_t magnitude = nominal() * (uint64_t)(ppm < 0 ? -ppm : ppm);
	uint64_t rounded = (2 * magnitude + 1000000) / 2000000;
	return ppm < 0 ? nominal() - rounded : nominal() + rounded;
}
static int16_t authored(uint64_t index, unsigned channel)
{
	return (int16_t)((int32_t)((index * (channel ? 4567 : 7919) + (channel ? 54321 : 101)) %
				   65536) -
			 32768);
}
static int16_t source_at(const struct reference *r, uint64_t index, unsigned channel)
{
	return authored(index + (index >= r->loss_boundary ? r->frames : 0), channel);
}
static int16_t independent_sample(const struct reference *r, unsigned channel)
{
	uint64_t index = r->coordinate / ONE;
	uint64_t fraction = r->coordinate % ONE;
	int64_t a = source_at(r, index, channel);
	int64_t b = source_at(r, index + 1, channel);
	/* Global piecewise-linear value, expressed as a + f*(b-a), not
	 * production extended-block history or its two weighted products. */
	int64_t numerator = a * (int64_t)ONE + (b - a) * (int64_t)fraction;
	uint64_t magnitude = (uint64_t)(numerator < 0 ? -numerator : numerator);
	int64_t rounded = (int64_t)((magnitude + ONE / 2) / ONE);
	return (int16_t)(numerator < 0 ? -rounded : rounded);
}
static bool oracle_write(const int16_t *actual, size_t frames, int16_t *expected, void *context)
{
	struct reference *r = context;
	unsigned ordinal = r->write_index++;
	if (r->startup && ordinal < 14) {
		uint64_t a = (uint64_t)ordinal * r->frames * 47619 / 48000;
		uint64_t b = (uint64_t)(ordinal + 1) * r->frames * 47619 / 48000;
		memset(expected, 0, frames * 4);
		bool valid = frames == b - a;
		for (size_t i = 0; i < frames * 2; i++) {
			if (actual[i]) {
				valid = false;
			}
		}
		r->wave_error |= !valid;
		return valid;
	}
	if (ordinal != (r->startup ? 14U : 0U)) {
		r->cadence_error = true;
		memset(expected, 0, frames * 4);
		return false;
	}
	int32_t ppm = r->startup ? 0 : applied_ppm;
	r->available += r->frames;
	uint64_t end = (r->available - 1) * ONE;
	uint64_t increment = step_for(ppm);
	size_t produced = 0;
	while (r->coordinate <= end) {
		if (produced >= 481) {
			r->wave_error = true;
			return false;
		}
		expected[2 * produced] = independent_sample(r, 0);
		expected[2 * produced + 1] = independent_sample(r, 1);
		produced++;
		r->coordinate += increment;
	}
	bool valid = produced == frames && memcmp(actual, expected, frames * 4) == 0;
	r->wave_error |= !valid;
	return valid;
}
static void wait_active(void)
{
	for (int i = 0; i < 2000 && !audio_offload_is_healthy(); i++) {
		k_sleep(K_MSEC(1));
	}
	zassert_true(audio_offload_is_healthy(), "Remote endpoint did not become ACTIVE");
}
static void begin(int pclk, unsigned frames, bool disabled)
{
	audio_sink_stop();
	audio_offload_stream_stop();
	clock_i2s_prepare(pclk);
	peer_fault = 0;
	peer_fail_after_fault = peer_fail_submissions = false;
	correction_disabled = disabled;
	applied_ppm = 0;
	observed_underruns = 0;
	zassert_equal(audio_sink_init(), 0);
	zassert_equal(audio_offload_init(), 0);
	audio_offload_stream_start();
	wait_active();
	audio_sink_set_input_frames(frames);
	zassert_equal(audio_sink_stream_open(), 0);
	audio_drift_frequency_error_update(pclk);
}

static enum violation recovery_verdict(bool *pending, uint64_t now, uint64_t deadline,
				       uint32_t prior_success,
				       const struct audio_offload_status *status)
{
	if (!*pending) {
		return OK;
	}
	/* A late success cannot erase an already exceeded recovery budget. */
	if (now > deadline) {
		return RECOVERY;
	}
	if (status->healthy && status->success_count > prior_success) {
		*pending = false;
	}
	return OK;
}

static enum violation run_case(const struct scenario *s, uint64_t duration_us, uint64_t settle_us)
{
	begin(s->pclk, s->frames, s->disabled);
	struct reference r = {.loss_boundary = UINT64_MAX, .frames = s->frames};
	clock_i2s_set_oracle(oracle_write, &r);
	peer_fail_after_fault = s->fail_remote_after_fault;
	struct clock_snapshot snapshot, tail = {0};
	uint64_t epoch = 0, measurement_second = UINT64_MAX, changed_at = UINT64_MAX;
	uint64_t emitted = 0, tail_emitted = 0;
	unsigned period = s->frames == 480 ? 10000 : 7500;
	int pclk = s->pclk;
	bool omitted = false, fault1 = false, fault2 = false, changed = false, tail_seen = false;
	bool awaiting_remote = false;
	uint64_t recovery_deadline = 0;
	uint32_t success_before_fault = 0;
	enum violation result = OK;
	int16_t input[960];
	printk("CLOCK_BEGIN case=%s frames=%u pclk=%d source=%d stimuli=%u disabled=%u "
	       "duration=%llu settle=%llu\n",
	       s->name, s->frames, s->pclk, s->source_ppm, s->stimuli, s->disabled,
	       (unsigned long long)duration_us, (unsigned long long)settle_us);
	printk("CLOCK_ROW_FIELDS "
	       "sequence,arrival_us,enqueue_us,clock_us,output_frames,applied_ppm,queued,"
	       "transferred\n");
	for (unsigned sequence = 0;; sequence++) {
		uint64_t due = (uint64_t)sequence * period * 1000000 / (1000000 + s->source_ppm);
		if (due >= duration_us) {
			break;
		}
		int jitter = (s->stimuli & JITTER) ? ((int)(sequence % 3) - 1) * 1000 : 0;
		uint64_t ready = epoch + due + (sequence ? jitter : 0);
		if (sequence) {
			uint64_t now = reference_time_us();
			if (now < ready) {
				k_sleep(K_USEC(ready - now));
			} else if (now - ready > period) {
				result = SOURCE_LATE;
				break;
			}
		}
		uint64_t elapsed = sequence ? reference_time_us() - epoch : 0;
		uint64_t arrival = reference_time_us();
		if ((s->stimuli & STEP) && !changed && elapsed >= 120000000) {
			changed_at = clock_i2s_set_skew(1000);
			pclk = 1000;
			changed = true;
			printk("CLOCK_EVENT case=%s event=step time=%llu pclk=%d\n", s->name,
			       (unsigned long long)changed_at, pclk);
		}
		if (elapsed / 1000000 != measurement_second) {
			audio_drift_frequency_error_update(pclk);
			measurement_second = elapsed / 1000000;
		}
		if ((s->stimuli & LOSS) && !omitted && elapsed >= 200000000) {
			r.loss_boundary = r.available;
			omitted = true;
			printk("CLOCK_EVENT case=%s event=omitted sequence=%u time=%llu\n", s->name,
			       sequence, (unsigned long long)reference_time_us());
			continue;
		}
		if (s->stimuli & DELAY) {
			k_sleep(K_USEC(sequence % 2 ? 2000 : 0));
		}
		int injected = 0;
		struct audio_offload_status before_status;
		struct audio_offload_asrc_stats before_asrc;
		audio_offload_get_status(&before_status);
		audio_offload_get_asrc_stats(&before_asrc);
		if ((s->stimuli & FAULTS) && s->frames == 480) {
			if (!fault1 && elapsed >= 300000000) {
				peer_fault = 1;
				fault1 = true;
				injected = 1;
			}
			if (!fault2 && elapsed >= 500000000) {
				peer_fault = 2;
				fault2 = true;
				injected = 2;
			}
		}
		if (injected) {
			zassert_true(before_status.healthy,
				     "Inject only into ACTIVE remote processing");
			awaiting_remote = true;
			success_before_fault = before_status.success_count;
			recovery_deadline = elapsed + 5000000;
			printk("CLOCK_EVENT case=%s event=remote-fault kind=%d time=%llu\n",
			       s->name, injected, (unsigned long long)reference_time_us());
		}
		for (unsigned i = 0; i < s->frames; i++) {
			input[2 * i] = authored((uint64_t)sequence * s->frames + i, 0);
			input[2 * i + 1] = authored((uint64_t)sequence * s->frames + i, 1);
		}
		clock_i2s_clear_capture();
		r.startup = sequence == 0;
		r.write_index = 0;
		int ret = audio_sink_push(input, s->frames * 2U);
		if (ret) {
			result = PUSH;
			break;
		}
		size_t n = clock_i2s_capture_count();
		if (n != (sequence ? 1U : 15U)) {
			result = REPEAT;
			break;
		}
		if (!sequence) {
			for (unsigned j = 0; j < 14; j++) {
				const struct clock_capture *c = clock_i2s_capture_get(j);
				uint64_t a = (uint64_t)j * s->frames * 47619 / 48000;
				uint64_t b = (uint64_t)(j + 1) * s->frames * 47619 / 48000;
				zassert_equal(c->frames, b - a, "Startup silence count");
				for (size_t k = 0; k < c->frames * 2; k++) {
					zassert_equal(c->pcm[k], 0);
				}
			}
		}
		const struct clock_capture *capture = clock_i2s_capture_get(n - 1);
		if (r.wave_error || r.cadence_error) {
			result = WAVEFORM;
			break;
		}
		emitted += capture->frames;
		clock_i2s_snapshot(&snapshot);
		struct audio_offload_status after_status;
		struct audio_offload_asrc_stats after_asrc;
		audio_offload_get_status(&after_status);
		audio_offload_get_asrc_stats(&after_asrc);
		if (injected) {
			zassert_equal(after_status.success_count, success_before_fault);
			zassert_equal(after_status.fallback_count,
				      before_status.fallback_count + 1);
			if (injected == 1) {
				zassert_equal(after_status.timeout_count,
					      before_status.timeout_count + 1);
			} else {
				zassert_equal(after_asrc.state_fault_count,
					      before_asrc.state_fault_count + 1);
			}
		}
		bool was_pending = awaiting_remote;
		if (recovery_verdict(&awaiting_remote, reference_time_us() - epoch,
				     recovery_deadline, success_before_fault,
				     &after_status) == RECOVERY) {
			result = RECOVERY;
			break;
		}
		if (was_pending && !awaiting_remote) {
			printk("CLOCK_EVENT case=%s event=remote-resumed time=%llu\n", s->name,
			       (unsigned long long)reference_time_us());
		}
		if (!sequence) {
			epoch = snapshot.epoch_us;
		}
		__uint128_t clock_numerator;
		if (changed_at != UINT64_MAX && snapshot.time_us >= changed_at) {
			clock_numerator =
				(__uint128_t)(changed_at - epoch) * 47619ULL * (1000000 + s->pclk) +
				(__uint128_t)(snapshot.time_us - changed_at) * 47619ULL *
					(1000000 + pclk);
		} else {
			clock_numerator = (__uint128_t)(snapshot.time_us - epoch) * 47619ULL *
					  (1000000 + pclk);
		}
		if (snapshot.transferred + snapshot.missing != clock_numerator / 1000000000000ULL ||
		    snapshot.submitted !=
			    snapshot.transferred + snapshot.remaining + snapshot.cancelled ||
		    snapshot.missing || snapshot.duplicates || snapshot.bad_words ||
		    observed_underruns) {
			result = CLOCK;
			break;
		}
		printk("R %u %llu %llu %llu %u %d %u %llu\n", sequence, (unsigned long long)arrival,
		       (unsigned long long)capture->time_us, (unsigned long long)snapshot.time_us,
		       (unsigned)capture->frames, sequence ? applied_ppm : 0, snapshot.queued,
		       (unsigned long long)snapshot.transferred);
		if (snapshot.time_us - epoch >= settle_us) {
			if (snapshot.queued < 10 || snapshot.queued > 12) {
				result = QUEUE;
				break;
			}
			if (!tail_seen) {
				tail = snapshot;
				tail_emitted = emitted;
				tail_seen = true;
			}
		}
	}
	if (result == OK && settle_us < duration_us) {
		zassert_true(tail_seen, "No settled interval examined");
		uint64_t min_frames = s->frames * ONE / step_for(CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP);
		uint64_t max_frames =
			s->frames * ONE / step_for(-CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP) + 1;
		uint64_t bound = 12 * max_frames - 9 * min_frames + 1;
		int64_t difference = (int64_t)(emitted - tail_emitted) -
				     (int64_t)(snapshot.transferred - tail.transferred);
		if (difference < -(int64_t)bound || difference > (int64_t)bound) {
			result = RATE;
		}
		if (s->stimuli & LOSS) {
			zassert_true(omitted);
		}
		struct audio_offload_status status;
		audio_offload_get_status(&status);
		if (s->frames == 480) {
			zassert_true(status.success_count > 0, "Remote success must be observed");
			if (s->stimuli & FAULTS) {
				zassert_true(fault1 && fault2 && status.fallback_count >= 2 &&
					     status.recovery_attempts >= 2);
				zassert_false(awaiting_remote,
					      "Final fault must resume remote processing");
			}
		}
	}
	audio_sink_stop();
	audio_offload_stream_stop();
	clock_i2s_snapshot(&snapshot);
	if (result == OK && (snapshot.bad_words || snapshot.missing)) {
		result = CLOCK;
	}
	zassert_equal(snapshot.queued, 0, "Stop must release descriptor ownership");
	zassert_equal(snapshot.remaining, 0);
	zassert_equal(snapshot.submitted, snapshot.transferred + snapshot.cancelled,
		      "Transferred and cancelled words must account for every submission");
	zassert_equal(audio_drift_get_ppm(), 0, "Public stop resets controller output");
	struct clock_snapshot idle_before = snapshot, idle_after;
	k_sleep(K_MSEC(5));
	clock_i2s_snapshot(&idle_after);
	zassert_equal(idle_after.transferred, idle_before.transferred);
	zassert_equal(idle_after.missing, idle_before.missing);
	zassert_equal(idle_after.time_us, idle_before.time_us,
		      "Stopped word clock cannot keep ticking");
	printk("CLOCK_RESULT case=%s violation=%u source_frames=%llu output_frames=%llu\n", s->name,
	       result, (unsigned long long)r.available, (unsigned long long)emitted);
	return result;
}

ZTEST(clock_loop, test_timed_closed_loop_and_lifecycle)
{
	const struct scenario cases[] = {
		{.name = "baseline480", .frames = 480},
		{.name = "fast480", .frames = 480, .pclk = 1000, .source_ppm = 100},
		{.name = "slow480", .frames = 480, .pclk = -1000, .source_ppm = -100},
		{.name = "step480", .frames = 480, .stimuli = STEP},
		{.name = "jitter480", .frames = 480, .pclk = 1000, .stimuli = JITTER},
		{.name = "loss480", .frames = 480, .pclk = 1000, .stimuli = LOSS},
		{.name = "delay480", .frames = 480, .pclk = 1000, .stimuli = DELAY},
		{.name = "faults480", .frames = 480, .pclk = 1000, .stimuli = FAULTS},
		{.name = "mixed360",
		 .frames = 360,
		 .pclk = 1000,
		 .source_ppm = 100,
		 .stimuli = JITTER | LOSS | DELAY},
		{.name = "slow360", .frames = 360, .pclk = -1000, .source_ppm = -100},
	};
	printk("CLOCK_TUNING output=%d integral=%d tick_us=%llu\n", CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP,
	       CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP,
	       (unsigned long long)k_ticks_to_us_floor64(1));
	for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
		zassert_equal(run_case(&cases[i], 1000000000ULL, 900000000ULL), OK, "Case %s",
			      cases[i].name);
		char name[64];
		snprintf(name, sizeof(name), "short_reopened_%s", cases[i].name);
		struct scenario reopened = {.name = name, .frames = cases[i].frames};
		/* Lifecycle waveform proof, not another redundant settling horizon. */
		zassert_equal(run_case(&reopened, 1000000, UINT64_MAX), OK);
	}
}
ZTEST(clock_loop, test_same_plant_requires_correction)
{
	const struct scenario cases[] = {
		{.name = "causal_fast480", .frames = 480, .pclk = 1000},
		{.name = "causal_slow360", .frames = 360, .pclk = -1000},
	};
	for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
		struct scenario paired = cases[i];
		zassert_equal(run_case(&paired, 1000000000ULL, 900000000ULL), OK,
			      "Exact enabled counterfactual");
		paired.disabled = true;
		enum violation observed = run_case(&paired, 1000000000ULL, 900000000ULL);
		zassert_true(observed == REPEAT || observed == SOURCE_LATE || observed == QUEUE ||
				     observed == RATE,
			     "Causal control must fail queue/rate/deadline/continuity, not "
			     "unrelated setup/waveform");
	}
}

ZTEST(clock_loop, test_permanent_remote_failure_is_not_recovery)
{
	struct scenario unavailable = {.name = "unavailable_after_fault",
				       .frames = 480,
				       .pclk = 1000,
				       .stimuli = FAULTS,
				       .fail_remote_after_fault = true};
	zassert_equal(run_case(&unavailable, 1000000000ULL, 900000000ULL), RECOVERY,
		      "Correct CPU waveform cannot replace successful remote recovery");
}

static void fill_input(int16_t *input, unsigned frames, uint64_t start)
{
	for (unsigned i = 0; i < frames; i++) {
		input[2 * i] = authored(start + i, 0);
		input[2 * i + 1] = authored(start + i, 1);
	}
}
static void verify_stopped_idle(bool corrupted)
{
	struct clock_snapshot before, after;
	clock_i2s_snapshot(&before);
	zassert_equal(before.queued, 0);
	zassert_equal(before.remaining, 0);
	zassert_equal(before.submitted, before.transferred + before.cancelled);
	zassert_equal(before.missing, 0, "Positive lifecycle cannot hide accumulated underflow");
	if (corrupted) {
		zassert_true(before.bad_words > 0);
	} else {
		zassert_equal(before.bad_words, 0,
			      "All timed lifecycle words must match independent oracle");
	}
	k_sleep(K_MSEC(5));
	clock_i2s_snapshot(&after);
	zassert_equal(after.time_us, before.time_us);
	zassert_equal(after.transferred, before.transferred);
	zassert_equal(after.missing, before.missing);
	zassert_equal(after.bad_words, before.bad_words);
}
ZTEST(clock_loop, test_consumed_words_detect_post_submission_corruption)
{
	begin(0, 480, false);
	struct reference r = {.loss_boundary = UINT64_MAX, .frames = 480, .startup = true};
	clock_i2s_set_oracle(oracle_write, &r);
	int16_t input[960];
	fill_input(input, 480, 0);
	zassert_equal(audio_sink_push(input, 960), 0);
	zassert_false(r.wave_error);
	unsigned key = irq_lock();
	int16_t *live = clock_i2s_next_owned_word();
	*live ^= 1;
	irq_unlock(key);
	/* Submitted capture remains correct. Timed consumption must read RAM. */
	zassert_equal(clock_i2s_capture_get(0)->pcm[0], 0);
	k_sleep(K_MSEC(1));
	struct clock_snapshot snapshot;
	clock_i2s_snapshot(&snapshot);
	zassert_true(snapshot.bad_words > 0,
		     "Timed output oracle must detect owned-buffer mutation");
	audio_sink_stop();
	audio_offload_stream_stop();
	verify_stopped_idle(true);
}

static K_THREAD_STACK_DEFINE(push_stack, 16384);
static struct k_thread push_thread;
static K_THREAD_STACK_DEFINE(stop_stack, 4096);
static struct k_thread stop_thread;
static int push_result;
static void blocked_push(void *data, void *unused1, void *unused2)
{
	(void)unused1;
	(void)unused2;
	push_result = audio_sink_push(data, 960);
}
static void bounded_stop(void *unused0, void *unused1, void *unused2)
{
	(void)unused0;
	(void)unused1;
	(void)unused2;
	audio_sink_stop();
}
ZTEST(clock_loop, test_stop_during_backpressure_and_reopen_without_plant_reset)
{
	begin(0, 480, false);
	struct reference r = {.loss_boundary = UINT64_MAX, .frames = 480, .startup = true};
	clock_i2s_set_oracle(oracle_write, &r);
	int16_t input[960];
	fill_input(input, 480, 0);
	zassert_equal(audio_sink_push(input, 960), 0);
	zassert_false(r.wave_error);
	fill_input(input, 480, 480);
	r.startup = false;
	r.write_index = 0;
	clock_i2s_clear_capture();
	push_result = -EINPROGRESS;
	k_thread_create(&push_thread, push_stack, K_THREAD_STACK_SIZEOF(push_stack), blocked_push,
			input, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_equal(clock_i2s_wait_blocked(K_MSEC(100)), 0,
		      "Producer must reach finite FIFO backpressure");
	k_thread_create(&stop_thread, stop_stack, K_THREAD_STACK_SIZEOF(stop_stack), bounded_stop,
			NULL, NULL, NULL, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	if (k_thread_join(&stop_thread, K_MSEC(1000)) ||
	    k_thread_join(&push_thread, K_MSEC(1000))) {
		printk("CLOCK_FAIL bounded producer/stop completion exceeded\n");
		/* End the owned test process; do not leave stuck workers contaminating
		 * later cases or hang the canonical gate. */
		exit(EXIT_FAILURE);
	}
	zassert_equal(push_result, 0,
		      "Clock completion must unblock admitted producer before stop finishes");
	zassert_false(r.wave_error || r.cadence_error);
	zassert_equal(clock_i2s_capture_count(), 1);
	zassert_equal(audio_sink_push(input, 960), -EBUSY, "Stop must close new admission");
	audio_offload_stream_stop();
	verify_stopped_idle(false);
	/* No clock_i2s_prepare or module-private reset can hide cleanup faults. */
	r = (struct reference){.loss_boundary = UINT64_MAX, .frames = 480, .startup = true};
	clock_i2s_set_oracle(oracle_write, &r);
	clock_i2s_clear_capture();
	audio_offload_stream_start();
	wait_active();
	zassert_equal(audio_sink_stream_open(), 0);
	audio_drift_frequency_error_update(0);
	fill_input(input, 480, 0);
	zassert_equal(audio_sink_push(input, 960), 0);
	zassert_false(r.wave_error || r.cadence_error);
	zassert_equal(clock_i2s_capture_count(), 15);
	for (unsigned sequence = 1; sequence <= 20; sequence++) {
		k_sleep(K_MSEC(10));
		r.startup = false;
		r.write_index = 0;
		clock_i2s_clear_capture();
		fill_input(input, 480, (uint64_t)sequence * 480);
		zassert_equal(audio_sink_push(input, 960), 0);
		zassert_false(r.wave_error || r.cadence_error);
		zassert_equal(clock_i2s_capture_count(), 1);
	}
	struct clock_snapshot consumed;
	clock_i2s_snapshot(&consumed);
	zassert_true(consumed.transferred > 14 * 477U,
		     "Reopened authored data must reach timed output, not only enqueue");
	zassert_equal(consumed.bad_words, 0);
	zassert_equal(consumed.missing, 0);
	audio_sink_stop();
	audio_offload_stream_stop();
	verify_stopped_idle(false);
}
ZTEST(clock_loop, test_recovery_deadline_cannot_be_erased_by_late_success)
{
	struct audio_offload_status success = {.healthy = true, .success_count = 11};
	bool pending = true;
	zassert_equal(recovery_verdict(&pending, 5000001, 5000000, 10, &success), RECOVERY);
	zassert_true(pending);
	zassert_equal(recovery_verdict(&pending, 5000000, 5000000, 10, &success), OK);
	zassert_false(pending);
}
ZTEST_SUITE(clock_loop, NULL, NULL, NULL, NULL, NULL);
