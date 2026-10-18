/* SPDX-License-Identifier: MIT */
/* Synthetic air-unit (role "ap") configuration in the original loader's shape:
 * an ap block with its slot receive policy, the broadcast/slot profiles the
 * AP runs, two-group calibration rows split at a sub-band edge. */
static const char ap_config_fixture[] =
"{\"baseband\":{\"basic\":{\"role\":\"ap\","
"\"ap\":{\"mac\":\"a1b2c3d4\",\"clock\":\"200MHz\",\"init_mcs\":-1,\"user_mode\":{\"mode\":\"1V1\",\"slot_bmp\":\"0x01\"},"
"\"mcs\":{\"slot\":{\"enable\":true,\"init\":-1,\"mode\":\"auto\",\"hold_time\":1000,\"max_wait_time\":5000,"
"\"mcs_tab\":[{\"mcs\":-1,\"snr_up\":60,\"snr_dw\":40,\"ldpc_up_num\":2,\"ldpc_dw_num\":4,\"up_keep_time\":1000,\"dw_keep_time\":500},"
"{\"mcs\":0,\"snr_up\":112,\"snr_dw\":72,\"ldpc_up_num\":2,\"ldpc_dw_num\":4,\"up_keep_time\":1000,\"dw_keep_time\":500}]}}},"
"\"dev\":{\"mac\":\"66000001\",\"ap_mac\":\"66000000\"},"
"\"power\":{\"mode\":\"closeloop\",\"pwr_init\":10,\"pwr_auto\":\"off\"},"
"\"lna\":{\"inner\":{\"mode\":\"manu\",\"manu\":\"on\"},\"fem\":{\"mode\":\"manu\",\"manu\":\"on\"}},"
"\"para\":{\"br\":{\"tx\":{\"payload\":true,\"bandwidth\":\"5\",\"enable_tintlv\":true,\"tintlv_num\":2,\"tintlv_len\":12,\"fch_info_len\":96,\"tx_mode\":\"2TX_STBC\",\"retx_count\":100},"
"\"rx\":{\"payload\":true,\"bandwidth\":\"5\",\"enable_tintlv\":true,\"tintlv_num\":2,\"tintlv_len\":12,\"fch_info_len\":96,\"rx_mode\":\"2T2R_STBC\"}},"
"\"slot\":{\"tx\":{\"payload\":true,\"bandwidth\":\"2.5\",\"enable_tintlv\":true,\"tintlv_num\":1,\"tintlv_len\":12,\"fch_info_len\":96,\"tx_mode\":\"1TX\",\"retx_count\":6},"
"\"rx\":{\"payload\":true,\"bandwidth\":\"2.5\",\"enable_tintlv\":true,\"tintlv_num\":1,\"tintlv_len\":12,\"fch_info_len\":96,\"rx_mode\":\"1T1R\"}}},"
"\"channel\":{\"band_mode\":\"band_single\",\"init_chan\":-1,\"init_band\":\"5G\",\"mode\":\"manu\",\"band_hop\":\"manu\",\"fs_bw\":\"5\","
"\"freq\":[5839000,5914000,5955000],\"multi_mode\":{\"enable\":false},\"rc_hop\":{\"enable\":false}},"
"\"dist_calc\":{\"enable\":true,\"window\":3,\"timeout\":63,\"offset\":20}},"
"\"power_calibration\":{\"default\":{\"sub_bands\":[5850000,5990000],\"closeloop\":{"
"\"9dbm\":[\"24\",\"30\",\"2A\",\"34\",\"1E\",\"36\",\"2A\",\"34\"],"
"\"10dbm\":[\"23\",\"31\",\"29\",\"35\",\"1D\",\"37\",\"29\",\"35\"]}}}}}";
