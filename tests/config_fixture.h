/* SPDX-License-Identifier: MIT */
/* Synthetic board/user settings; no vendor payload or factory identity. */
static const char config_fixture[] =
"{\"baseband\":{\"basic\":{"
"\"role\":\"dev\",\"dev\":{\"clock\":\"200MHz\",\"mac\":\"12345678\",\"ap_mac\":\"87654321\","
"\"mcs\":{\"slot\":{\"enable\":true,\"init\":-1,\"mode\":\"auto\",\"hold_time\":101,\"max_wait_time\":4001,"
"\"bw_auto\":{\"enable\":false},\"mcs_tab\":["
"{\"mcs\":-1,\"ldpc_up_num\":0,\"ldpc_dw_num\":3,\"ldpc_dw_conti_num\":2,\"snr_up\":40,\"snr_dw\":30,\"up_keep_time\":901,\"dw_keep_time\":13},"
"{\"mcs\":0,\"ldpc_up_num\":1,\"ldpc_dw_num\":4,\"ldpc_dw_conti_num\":3,\"snr_up\":90,\"snr_dw\":60,\"up_keep_time\":902,\"dw_keep_time\":14}]}}},"
"\"channel\":{\"band_mode\":\"band_single\",\"init_band\":\"5G\",\"mode\":\"manu\",\"band_hop\":\"manu\",\"fs_bw\":\"5\","
"\"multi_mode\":{\"enable\":false},\"rc_hop\":{\"enable\":false},\"freq\":[4915000,5220000,5770000],\"init_chan\":-1},"
"\"power\":{\"mode\":\"closeloop\",\"pwr_auto\":\"manu\",\"link_auto\":\"manu\",\"manu_init\":25},"
"\"lna\":{\"inner\":{\"mode\":\"manu\",\"manu\":\"on\"},\"fem\":{\"mode\":\"manu\",\"manu\":\"on\"}},"
"\"para\":{\"br\":{\"tx\":{\"bandwidth\":\"5\",\"payload\":true,\"enable_tintlv\":true,\"tintlv_num\":2,\"tintlv_len\":12,\"fch_info_len\":96,\"tx_mode\":\"2TX\"},\"rx\":{\"bandwidth\":\"5\",\"payload\":true,\"enable_tintlv\":true,\"tintlv_num\":2,\"tintlv_len\":12,\"fch_info_len\":96,\"rx_mode\":\"2T2R_STBC\"}},"
"\"slot\":{\"tx\":{\"bandwidth\":\"2.5\",\"payload\":true,\"enable_tintlv\":true,\"tintlv_num\":1,\"tintlv_len\":12,\"fch_info_len\":96,\"tx_mode\":\"2TX_STBC\",\"retx_count\":7},\"rx\":{\"bandwidth\":\"2.5\",\"payload\":true,\"enable_tintlv\":true,\"tintlv_num\":1,\"tintlv_len\":12,\"fch_info_len\":96,\"rx_mode\":\"2T2R_STBC\"}}},\"dist_calc\":{\"enable\":true}},"
"\"power_calibration\":{\"default\":{\"closeloop\":{\"25dbm\":[\"11,\",\"4A,\",\"12,\",\"4B\"]}}}}}";
