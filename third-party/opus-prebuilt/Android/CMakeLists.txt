cmake_minimum_required(VERSION 3.4.1)

set(MY_SOURCE_DIR ../../opus)

file(GLOB MY_FLOAT_SOURCE_DIR ${MY_SOURCE_DIR}/silk/float/*.c*)

set(MY_C_FLAGS
    "-DNULL=0 -DSOCKLEN_T=socklen_t -DLOCALE_NOT_USED -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64 \
     -D__EMX__ -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -O3 -fno-math-errno -fvisibility=hidden"
    )

set(MY_CXX_FLAGS
    "-DBSD=1 \
     -ffast-math -O3 -funroll-loops"
    )

set(CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} ${MY_C_FLAGS}"
    )

set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} \
     ${MY_C_FLAGS} \
     ${MY_CXX_FLAGS}"
    )

add_library(opus_static

            STATIC

            ${MY_SOURCE_DIR}/src/analysis.c
            ${MY_SOURCE_DIR}/src/mlp.c
            ${MY_SOURCE_DIR}/src/mlp_data.c
            ${MY_SOURCE_DIR}/src/opus.c
            ${MY_SOURCE_DIR}/src/opus_decoder.c
            ${MY_SOURCE_DIR}/src/opus_encoder.c
            ${MY_SOURCE_DIR}/src/opus_multistream.c
            ${MY_SOURCE_DIR}/src/opus_multistream_decoder.c
            ${MY_SOURCE_DIR}/src/opus_multistream_encoder.c
            ${MY_SOURCE_DIR}/src/repacketizer.c
            ${MY_SOURCE_DIR}/celt/bands.c
            ${MY_SOURCE_DIR}/celt/celt.c
            ${MY_SOURCE_DIR}/celt/celt_decoder.c
            ${MY_SOURCE_DIR}/celt/celt_encoder.c
            ${MY_SOURCE_DIR}/celt/celt_lpc.c
            ${MY_SOURCE_DIR}/celt/cwrs.c
            ${MY_SOURCE_DIR}/celt/entcode.c
            ${MY_SOURCE_DIR}/celt/entdec.c
            ${MY_SOURCE_DIR}/celt/entenc.c
            ${MY_SOURCE_DIR}/celt/kiss_fft.c
            ${MY_SOURCE_DIR}/celt/laplace.c
            ${MY_SOURCE_DIR}/celt/mathops.c
            ${MY_SOURCE_DIR}/celt/mdct.c
            ${MY_SOURCE_DIR}/celt/modes.c
            ${MY_SOURCE_DIR}/celt/pitch.c
            ${MY_SOURCE_DIR}/celt/quant_bands.c
            ${MY_SOURCE_DIR}/celt/rate.c
            ${MY_SOURCE_DIR}/celt/vq.c
            ${MY_SOURCE_DIR}/silk/A2NLSF.c
            ${MY_SOURCE_DIR}/silk/ana_filt_bank_1.c
            ${MY_SOURCE_DIR}/silk/biquad_alt.c
            ${MY_SOURCE_DIR}/silk/bwexpander_32.c
            ${MY_SOURCE_DIR}/silk/bwexpander.c
            ${MY_SOURCE_DIR}/silk/check_control_input.c
            ${MY_SOURCE_DIR}/silk/CNG.c
            ${MY_SOURCE_DIR}/silk/code_signs.c
            ${MY_SOURCE_DIR}/silk/control_audio_bandwidth.c
            ${MY_SOURCE_DIR}/silk/control_codec.c
            ${MY_SOURCE_DIR}/silk/control_SNR.c
            ${MY_SOURCE_DIR}/silk/debug.c
            ${MY_SOURCE_DIR}/silk/dec_API.c
            ${MY_SOURCE_DIR}/silk/decode_core.c
            ${MY_SOURCE_DIR}/silk/decode_frame.c
            ${MY_SOURCE_DIR}/silk/decode_indices.c
            ${MY_SOURCE_DIR}/silk/decode_parameters.c
            ${MY_SOURCE_DIR}/silk/decode_pitch.c
            ${MY_SOURCE_DIR}/silk/decode_pulses.c
            ${MY_SOURCE_DIR}/silk/decoder_set_fs.c
            ${MY_SOURCE_DIR}/silk/enc_API.c
            ${MY_SOURCE_DIR}/silk/encode_indices.c
            ${MY_SOURCE_DIR}/silk/encode_pulses.c
            ${MY_SOURCE_DIR}/silk/gain_quant.c
            ${MY_SOURCE_DIR}/silk/HP_variable_cutoff.c
            ${MY_SOURCE_DIR}/silk/init_decoder.c
            ${MY_SOURCE_DIR}/silk/init_encoder.c
            ${MY_SOURCE_DIR}/silk/inner_prod_aligned.c
            ${MY_SOURCE_DIR}/silk/interpolate.c
            ${MY_SOURCE_DIR}/silk/lin2log.c
            ${MY_SOURCE_DIR}/silk/log2lin.c
            ${MY_SOURCE_DIR}/silk/LPC_analysis_filter.c
            ${MY_SOURCE_DIR}/silk/LPC_inv_pred_gain.c
            ${MY_SOURCE_DIR}/silk/LP_variable_cutoff.c
            ${MY_SOURCE_DIR}/silk/NLSF2A.c
            ${MY_SOURCE_DIR}/silk/NLSF_decode.c
            ${MY_SOURCE_DIR}/silk/NLSF_del_dec_quant.c
            ${MY_SOURCE_DIR}/silk/NLSF_encode.c
            ${MY_SOURCE_DIR}/silk/NLSF_stabilize.c
            ${MY_SOURCE_DIR}/silk/NLSF_unpack.c
            ${MY_SOURCE_DIR}/silk/NLSF_VQ.c
            ${MY_SOURCE_DIR}/silk/NLSF_VQ_weights_laroia.c
            ${MY_SOURCE_DIR}/silk/NSQ.c
            ${MY_SOURCE_DIR}/silk/NSQ_del_dec.c
            ${MY_SOURCE_DIR}/silk/pitch_est_tables.c
            ${MY_SOURCE_DIR}/silk/PLC.c
            ${MY_SOURCE_DIR}/silk/process_NLSFs.c
            ${MY_SOURCE_DIR}/silk/quant_LTP_gains.c
            ${MY_SOURCE_DIR}/silk/resampler.c
            ${MY_SOURCE_DIR}/silk/resampler_down2_3.c
            ${MY_SOURCE_DIR}/silk/resampler_down2.c
            ${MY_SOURCE_DIR}/silk/resampler_private_AR2.c
            ${MY_SOURCE_DIR}/silk/resampler_private_down_FIR.c
            ${MY_SOURCE_DIR}/silk/resampler_private_IIR_FIR.c
            ${MY_SOURCE_DIR}/silk/resampler_private_up2_HQ.c
            ${MY_SOURCE_DIR}/silk/resampler_rom.c
            ${MY_SOURCE_DIR}/silk/shell_coder.c
            ${MY_SOURCE_DIR}/silk/sigm_Q15.c
            ${MY_SOURCE_DIR}/silk/sort.c
            ${MY_SOURCE_DIR}/silk/stereo_decode_pred.c
            ${MY_SOURCE_DIR}/silk/stereo_encode_pred.c
            ${MY_SOURCE_DIR}/silk/stereo_find_predictor.c
            ${MY_SOURCE_DIR}/silk/stereo_LR_to_MS.c
            ${MY_SOURCE_DIR}/silk/stereo_MS_to_LR.c
            ${MY_SOURCE_DIR}/silk/stereo_quant_pred.c
            ${MY_SOURCE_DIR}/silk/sum_sqr_shift.c
            ${MY_SOURCE_DIR}/silk/table_LSF_cos.c
            ${MY_SOURCE_DIR}/silk/tables_gain.c
            ${MY_SOURCE_DIR}/silk/tables_LTP.c
            ${MY_SOURCE_DIR}/silk/tables_NLSF_CB_NB_MB.c
            ${MY_SOURCE_DIR}/silk/tables_NLSF_CB_WB.c
            ${MY_SOURCE_DIR}/silk/tables_other.c
            ${MY_SOURCE_DIR}/silk/tables_pitch_lag.c
            ${MY_SOURCE_DIR}/silk/tables_pulses_per_block.c
            ${MY_SOURCE_DIR}/silk/VAD.c
            ${MY_SOURCE_DIR}/silk/VQ_WMat_EC.c

            ${MY_FLOAT_SOURCE_DIR}
            )

target_include_directories(
            opus_static 
            
            PRIVATE
            
            ${MY_SOURCE_DIR}/include
            ${MY_SOURCE_DIR}/silk
            ${MY_SOURCE_DIR}/silk/float
            ${MY_SOURCE_DIR}/celt
            )