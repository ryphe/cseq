@echo off
setlocal

echo =======================================================
echo building cseq
echo =======================================================

:: 1. Detect which .rc file exists
set "RC_NAME="
if exist cseq.rc set "RC_NAME=cseq.rc"
if exist resource.rc set "RC_NAME=resource.rc"

:: 2. Compile the resource file (only if .rc exists and Inter.ttf is present)
set "RES_FILE="
if defined RC_NAME (
    if not exist Inter.ttf (
        echo [WARN] Inter.ttf not found. Skipping resource compilation.
        echo        Font will be loaded from disk at runtime.
    ) else (
        echo [INFO] Compiling resources from %RC_NAME%...
        rc /nologo /I headers /fo cseq.res %RC_NAME%
        if %ERRORLEVEL% EQU 0 if exist cseq.res (
            set "RES_FILE=cseq.res"
            echo [INFO] Resource compiled successfully: %RES_FILE%
        ) else (
            echo [WARN] Resource compilation failed. Proceeding without embedded font.
        )
    )
)

:: 3. Compile the C source files
set "INC_FLAG="
if exist headers set "INC_FLAG=/Iheaders"

:: Optional profiling harness (§0): set cseq_PROFILE=1 to emit
:: rebuild/strip timings + GDI handle counts via OutputDebugString
set "EXTRA_DEFS="
if defined cseq_PROFILE set "EXTRA_DEFS=/Dcseq_PROFILE"

:: Optional SIMD level: set cseq_AVX2=1 to compile the 8-wide AVX2
:: track-summing path (runtime CPU check still guards it). Default is
:: the SSE2 baseline on x64 with a scalar fallback.
set "ARCH_FLAGS="
if defined cseq_AVX2 set "ARCH_FLAGS=/arch:AVX2"

echo [INFO] Compiling C source files...

:: Project code is held to /W4 /WX. Vendored code - miniaudio, vorbis, and
:: the opus decoder stack (libogg + libopus + libopusfile) - is compiled
:: separately at /W3 with leniency so upstream warnings never fail the build.
:: All objects are linked together with /LTCG below.
set "COMMON_FLAGS=/DNDEBUG /MT /O1 /Os /Oi /Gy /Gw /GS- /fp:fast %ARCH_FLAGS% /GL /std:c17 /permissive- /Zc:inline /Zc:strictStrings %EXTRA_DEFS% /Iheaders /Iopus"
set "PROJ_FLAGS=%COMMON_FLAGS% /W4 /WX /wd4324"
set "VEND_FLAGS=%COMMON_FLAGS% /W3 /wd4244 /wd4267 /wd4456 /wd4457 /wd4701 /wd4702 /wd4703 /wd5045 /wd4191"

:: The vendored opus decoder stack is compiled WITHOUT /Iheaders so each
:: library's own "config.h" (opus/src/config.h, opusfile/src/config.h) is the
:: one that resolves, never c1.33's headers/config.h (which lacks the opus
:: build defines). /Iheaders is what provides c1.33's config.h for the wrapper.
set "OPUS_VEND_FLAGS=/DNDEBUG /MT /O1 /Os /Oi /Gy /Gw /GS- /fp:fast %ARCH_FLAGS% /GL /std:c17 /permissive- /Zc:inline /Zc:strictStrings %EXTRA_DEFS% /W3 /wd4244 /wd4267 /wd4456 /wd4457 /wd4701 /wd4702 /wd4703 /wd5045 /wd4191"

:: Include roots for the vendored opus decoder stack.
set "OPUS_INC=-Iopus -Iopus/opus/include -Iopus/opus/src -Iopus/opus/celt -Iopus/opus/silk -Iopus/opus/silk/float -Iopus/opusfile/include -Iopus/opusfile/src -Iopus/ogg/include"
:: The wrapper's own "config.h" must resolve to c1.33's headers/config.h (for
:: SAMPLE_RATE), never to a libopus config.h, so it gets a dedicated include
:: set without -Iopus/opus/src. /Iheaders (in VEND_FLAGS) provides config.h.
set "OPUS_WRAP_INC=-Iopus -Iopus/opus/include -Iopus/opusfile/include -Iopus/ogg/include"

cl /nologo /c %PROJ_FLAGS% main.c eq.c
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - project sources
    exit /b %ERRORLEVEL%
)
cl /nologo /c %VEND_FLAGS% vorbis.c miniaudio.c
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - vendored sources
    exit /b %ERRORLEVEL%
)
echo [INFO] Compiling vendored opus decoder stack (libogg + libopus + libopusfile)...
cl /nologo /c %VEND_FLAGS% %OPUS_WRAP_INC% opus/opus_wrap.c >nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - opus wrapper
    exit /b %ERRORLEVEL%
)
cl /nologo /c %OPUS_VEND_FLAGS% %OPUS_INC% opus/ogg/src/framing.c opus/ogg/src/bitwise.c >nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - vendored libogg
    exit /b %ERRORLEVEL%
)
cl /nologo /c %OPUS_VEND_FLAGS% %OPUS_INC% /DHAVE_CONFIG_H ^
    opus/opus/src/opus.c opus/opus/src/opus_decoder.c opus/opus/src/opus_multistream.c opus/opus/src/opus_multistream_decoder.c opus/opus/src/extensions.c ^
    opus/opus/celt/bands.c opus/opus/celt/celt.c opus/opus/celt/celt_encoder.c opus/opus/celt/celt_decoder.c opus/opus/celt/cwrs.c opus/opus/celt/entcode.c opus/opus/celt/entdec.c opus/opus/celt/entenc.c opus/opus/celt/kiss_fft.c opus/opus/celt/laplace.c opus/opus/celt/mathops.c opus/opus/celt/mdct.c opus/opus/celt/modes.c opus/opus/celt/pitch.c opus/opus/celt/celt_lpc.c opus/opus/celt/quant_bands.c opus/opus/celt/rate.c opus/opus/celt/vq.c ^
    opus/opus/silk/CNG.c opus/opus/silk/code_signs.c opus/opus/silk/init_decoder.c opus/opus/silk/decode_core.c opus/opus/silk/decode_frame.c opus/opus/silk/decode_parameters.c opus/opus/silk/decode_indices.c opus/opus/silk/decode_pulses.c opus/opus/silk/decoder_set_fs.c opus/opus/silk/dec_API.c opus/opus/silk/enc_API.c opus/opus/silk/encode_indices.c opus/opus/silk/encode_pulses.c opus/opus/silk/gain_quant.c opus/opus/silk/interpolate.c opus/opus/silk/LP_variable_cutoff.c opus/opus/silk/NLSF_decode.c opus/opus/silk/NSQ.c opus/opus/silk/NSQ_del_dec.c opus/opus/silk/PLC.c opus/opus/silk/shell_coder.c opus/opus/silk/tables_gain.c opus/opus/silk/tables_LTP.c opus/opus/silk/tables_NLSF_CB_NB_MB.c opus/opus/silk/tables_NLSF_CB_WB.c opus/opus/silk/tables_other.c opus/opus/silk/tables_pitch_lag.c opus/opus/silk/tables_pulses_per_block.c opus/opus/silk/VAD.c opus/opus/silk/control_audio_bandwidth.c opus/opus/silk/quant_LTP_gains.c opus/opus/silk/VQ_WMat_EC.c opus/opus/silk/HP_variable_cutoff.c opus/opus/silk/NLSF_encode.c opus/opus/silk/NLSF_VQ.c opus/opus/silk/NLSF_unpack.c opus/opus/silk/NLSF_del_dec_quant.c opus/opus/silk/process_NLSFs.c opus/opus/silk/stereo_LR_to_MS.c opus/opus/silk/stereo_MS_to_LR.c opus/opus/silk/check_control_input.c opus/opus/silk/control_SNR.c opus/opus/silk/init_encoder.c opus/opus/silk/control_codec.c opus/opus/silk/A2NLSF.c opus/opus/silk/ana_filt_bank_1.c opus/opus/silk/biquad_alt.c opus/opus/silk/bwexpander_32.c opus/opus/silk/bwexpander.c opus/opus/silk/debug.c opus/opus/silk/decode_pitch.c opus/opus/silk/inner_prod_aligned.c opus/opus/silk/lin2log.c opus/opus/silk/log2lin.c opus/opus/silk/LPC_analysis_filter.c opus/opus/silk/LPC_inv_pred_gain.c opus/opus/silk/table_LSF_cos.c opus/opus/silk/NLSF2A.c opus/opus/silk/NLSF_stabilize.c opus/opus/silk/NLSF_VQ_weights_laroia.c opus/opus/silk/pitch_est_tables.c opus/opus/silk/resampler.c opus/opus/silk/resampler_down2_3.c opus/opus/silk/resampler_down2.c opus/opus/silk/resampler_private_AR2.c opus/opus/silk/resampler_private_down_FIR.c opus/opus/silk/resampler_private_IIR_FIR.c opus/opus/silk/resampler_private_up2_HQ.c opus/opus/silk/resampler_rom.c opus/opus/silk/sigm_Q15.c opus/opus/silk/sort.c opus/opus/silk/sum_sqr_shift.c opus/opus/silk/stereo_decode_pred.c opus/opus/silk/stereo_encode_pred.c opus/opus/silk/stereo_find_predictor.c opus/opus/silk/stereo_quant_pred.c opus/opus/silk/LPC_fit.c ^
    opus/opus/silk/float/apply_sine_window_FLP.c opus/opus/silk/float/corrMatrix_FLP.c opus/opus/silk/float/encode_frame_FLP.c opus/opus/silk/float/find_LPC_FLP.c opus/opus/silk/float/find_LTP_FLP.c opus/opus/silk/float/find_pitch_lags_FLP.c opus/opus/silk/float/find_pred_coefs_FLP.c opus/opus/silk/float/LPC_analysis_filter_FLP.c opus/opus/silk/float/LTP_analysis_filter_FLP.c opus/opus/silk/float/LTP_scale_ctrl_FLP.c opus/opus/silk/float/noise_shape_analysis_FLP.c opus/opus/silk/float/process_gains_FLP.c opus/opus/silk/float/regularize_correlations_FLP.c opus/opus/silk/float/residual_energy_FLP.c opus/opus/silk/float/warped_autocorrelation_FLP.c opus/opus/silk/float/wrappers_FLP.c opus/opus/silk/float/autocorrelation_FLP.c opus/opus/silk/float/burg_modified_FLP.c opus/opus/silk/float/bwexpander_FLP.c opus/opus/silk/float/energy_FLP.c opus/opus/silk/float/inner_product_FLP.c opus/opus/silk/float/k2a_FLP.c opus/opus/silk/float/LPC_inv_pred_gain_FLP.c opus/opus/silk/float/pitch_analysis_core_FLP.c opus/opus/silk/float/scale_copy_vector_FLP.c opus/opus/silk/float/scale_vector_FLP.c opus/opus/silk/float/schur_FLP.c opus/opus/silk/float/sort_FLP.c ^
    opus/opusfile/src/info.c opus/opusfile/src/internal.c opus/opusfile/src/opusfile.c opus/opusfile/src/stream.c >nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - vendored opus decoder
    exit /b %ERRORLEVEL%
)

set "LINK_INPUTS=main.obj eq.obj vorbis.obj miniaudio.obj opus_wrap.obj framing.obj bitwise.obj opus.obj opus_decoder.obj opus_multistream.obj opus_multistream_decoder.obj extensions.obj bands.obj celt.obj celt_encoder.obj celt_decoder.obj cwrs.obj entcode.obj entdec.obj entenc.obj kiss_fft.obj laplace.obj mathops.obj mdct.obj modes.obj pitch.obj celt_lpc.obj quant_bands.obj rate.obj vq.obj CNG.obj code_signs.obj init_decoder.obj decode_core.obj decode_frame.obj decode_parameters.obj decode_indices.obj decode_pulses.obj decoder_set_fs.obj dec_API.obj enc_API.obj encode_indices.obj encode_pulses.obj gain_quant.obj interpolate.obj LP_variable_cutoff.obj NLSF_decode.obj NSQ.obj NSQ_del_dec.obj PLC.obj shell_coder.obj tables_gain.obj tables_LTP.obj tables_NLSF_CB_NB_MB.obj tables_NLSF_CB_WB.obj tables_other.obj tables_pitch_lag.obj tables_pulses_per_block.obj VAD.obj control_audio_bandwidth.obj quant_LTP_gains.obj VQ_WMat_EC.obj HP_variable_cutoff.obj NLSF_encode.obj NLSF_VQ.obj NLSF_unpack.obj NLSF_del_dec_quant.obj process_NLSFs.obj stereo_LR_to_MS.obj stereo_MS_to_LR.obj check_control_input.obj control_SNR.obj init_encoder.obj control_codec.obj A2NLSF.obj ana_filt_bank_1.obj biquad_alt.obj bwexpander_32.obj bwexpander.obj debug.obj decode_pitch.obj inner_prod_aligned.obj lin2log.obj log2lin.obj LPC_analysis_filter.obj LPC_inv_pred_gain.obj table_LSF_cos.obj NLSF2A.obj NLSF_stabilize.obj NLSF_VQ_weights_laroia.obj pitch_est_tables.obj resampler.obj resampler_down2_3.obj resampler_down2.obj resampler_private_AR2.obj resampler_private_down_FIR.obj resampler_private_IIR_FIR.obj resampler_private_up2_HQ.obj resampler_rom.obj sigm_Q15.obj sort.obj sum_sqr_shift.obj stereo_decode_pred.obj stereo_encode_pred.obj stereo_find_predictor.obj stereo_quant_pred.obj LPC_fit.obj apply_sine_window_FLP.obj corrMatrix_FLP.obj encode_frame_FLP.obj find_LPC_FLP.obj find_LTP_FLP.obj find_pitch_lags_FLP.obj find_pred_coefs_FLP.obj LPC_analysis_filter_FLP.obj LTP_analysis_filter_FLP.obj LTP_scale_ctrl_FLP.obj noise_shape_analysis_FLP.obj process_gains_FLP.obj regularize_correlations_FLP.obj residual_energy_FLP.obj warped_autocorrelation_FLP.obj wrappers_FLP.obj autocorrelation_FLP.obj burg_modified_FLP.obj bwexpander_FLP.obj energy_FLP.obj inner_product_FLP.obj k2a_FLP.obj LPC_inv_pred_gain_FLP.obj pitch_analysis_core_FLP.obj scale_copy_vector_FLP.obj scale_vector_FLP.obj schur_FLP.obj sort_FLP.obj info.obj internal.obj opusfile.obj stream.obj"
if defined RES_FILE set "LINK_INPUTS=%LINK_INPUTS% %RES_FILE%"

link /nologo /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG /FILEALIGN:512 %LINK_INPUTS% user32.lib gdi32.lib shell32.lib comdlg32.lib msimg32.lib ole32.lib winmm.lib /OUT:cseq.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

:: Optional: Clean up intermediate build artifacts
del *.res *.obj 2>nul

echo [SUCCESS] Build complete: cseq.exe
endlocal