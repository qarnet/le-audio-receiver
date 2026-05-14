/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_stats.h"
#include "audio_drift.h"
#include "audio_volume.h"
#include "audio_i2s.h"

#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct audio_stats s = audio_stats_get();

	uint32_t plc_pct = s.total_frames > 0
		? (s.plc_frames * 100U) / s.total_frames
		: 0U;

	shell_print(sh, "--- Audio status ---");
	shell_print(sh, "  Frames decoded : %u", s.total_frames);
	shell_print(sh, "  PLC frames     : %u (%u%%)", s.plc_frames, plc_pct);
	shell_print(sh, "  Decode errors  : %u", s.decode_errors);
	shell_print(sh, "  I2S underruns  : %u", s.i2s_underruns);
	shell_print(sh, "  Stream resets  : %u", s.stream_resets);
	shell_print(sh, "  APLL state     : %s", audio_drift_state_str());
	shell_print(sh, "  Volume         : %u / 255%s",
		    audio_volume_get(),
		    audio_volume_is_muted() ? " (muted)" : "");

	return 0;
}

static int cmd_reset_stats(const struct shell *sh, size_t argc, char **argv)
{
	audio_stats_reset();
	shell_print(sh, "Stats cleared.");
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	audio_i2s_stop();
	shell_print(sh, "I2S stopped; drift reset.");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(audio_cmds,
	SHELL_CMD_ARG(status,      NULL, "Print audio stats and state.", cmd_status,      1, 0),
	SHELL_CMD_ARG(reset-stats, NULL, "Clear all counters.",          cmd_reset_stats, 1, 0),
	SHELL_CMD_ARG(stop,        NULL, "Stop I2S and reset drift.",    cmd_stop,        1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(audio, &audio_cmds, "LE Audio sink commands.", NULL);
