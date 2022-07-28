
#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <malloc.h>
#else
    #include <stdint.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

#include "nalu.h"

namespace sio {

namespace live555 {

////////////////////////////////////////////////////////////////////////////////////////////////
static unsigned int _read_bit(unsigned char* pStream, int& nCurrentBit)
{
    int nIndex = nCurrentBit / 8;
    int nOffset = nCurrentBit % 8 + 1;

    nCurrentBit ++;
    return (pStream[nIndex] >> (8-nOffset)) & 0x01;
}

static unsigned int _read_bits(int n, unsigned char* pStream, int& nCurrentBit)
{
    int r = 0;
    int i;
    for (i = 0; i < n; i++) {
        r |= ( _read_bit(pStream, nCurrentBit) << ( n - i - 1 ) );
    }
    return r;
}

static unsigned int _read_exponential_golomb_code(unsigned char* pStream, int& nCurrentBit)
{
    int r = 0;
    int i = 0;

    while( (_read_bit(pStream, nCurrentBit) == 0) && (i < 32) ) {
        i++;
    }

    r = _read_bits(i, pStream, nCurrentBit);
    r += (1 << i) - 1;
    return r;
}

static unsigned int _read_se(unsigned char* pStream, int& nCurrentBit)
{
    int r = _read_exponential_golomb_code(pStream, nCurrentBit);
    if (r & 0x01) {
        r = (r+1)/2;
    } else {
        r = -(r/2);
    }
    return r;
}

void svlive555_parse_sps(unsigned char * pStart, unsigned short _nLen, int* w, int* h, int* profile, int* level)
{
#define ReadBits(a) _read_bits(a, pStart, nCurrentBit)
#define ReadBit() _read_bit(pStart, nCurrentBit)
#define ReadSE() _read_se(pStart, nCurrentBit)
#define ReadExponentialGolombCode() _read_exponential_golomb_code(pStart, nCurrentBit)

    int nCurrentBit = 0;

    int frame_crop_left_offset=0;
    int frame_crop_right_offset=0;
    int frame_crop_top_offset=0;
    int frame_crop_bottom_offset=0;

    int skipBits = ReadBits(8); // forbidden_zero_bit; nal_ref_idc; nal_unit_type
    int profile_idc = ReadBits(8);
    int constraint_set0_flag = ReadBit();
    int constraint_set1_flag = ReadBit();
    int constraint_set2_flag = ReadBit();
    int constraint_set3_flag = ReadBit();
    int constraint_set4_flag = ReadBit();
    int constraint_set5_flag = ReadBit();
    int reserved_zero_2bits  = ReadBits(2);
    int level_idc = ReadBits(8);
    int seq_parameter_set_id = ReadExponentialGolombCode();


    if( profile_idc == 100 || profile_idc == 110 ||
        profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44 || profile_idc == 83 ||
        profile_idc == 86 || profile_idc == 118 ) {
        int chroma_format_idc = ReadExponentialGolombCode();

        if( chroma_format_idc == 3 ) {
            int residual_colour_transform_flag = ReadBit();
        }
        int bit_depth_luma_minus8 = ReadExponentialGolombCode();
        int bit_depth_chroma_minus8 = ReadExponentialGolombCode();
        int qpprime_y_zero_transform_bypass_flag = ReadBit();
        int seq_scaling_matrix_present_flag = ReadBit();

        if (seq_scaling_matrix_present_flag) {
            int i=0;
            for ( i = 0; i < 8; i++) {
                int seq_scaling_list_present_flag = ReadBit();
                if (seq_scaling_list_present_flag) {
                    int sizeOfScalingList = (i < 6) ? 16 : 64;
                    int lastScale = 8;
                    int nextScale = 8;
                    int j=0;
                    for ( j = 0; j < sizeOfScalingList; j++) {
                        if (nextScale != 0) {
                            int delta_scale = ReadSE();
                            nextScale = (lastScale + delta_scale + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    int log2_max_frame_num_minus4 = ReadExponentialGolombCode();
    int pic_order_cnt_type = ReadExponentialGolombCode();
    if( pic_order_cnt_type == 0 ){
        int log2_max_pic_order_cnt_lsb_minus4 = ReadExponentialGolombCode();
    } else if( pic_order_cnt_type == 1 ) {
        int delta_pic_order_always_zero_flag = ReadBit();
        int offset_for_non_ref_pic = ReadSE();
        int offset_for_top_to_bottom_field = ReadSE();
        int num_ref_frames_in_pic_order_cnt_cycle = ReadExponentialGolombCode();
        int i;
        for( i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++ ) {
            ReadSE();
            //sps->offset_for_ref_frame[ i ] = ReadSE();
        }
    }
    int max_num_ref_frames = ReadExponentialGolombCode();
    int gaps_in_frame_num_value_allowed_flag = ReadBit();
    int pic_width_in_mbs_minus1 = ReadExponentialGolombCode();
    int pic_height_in_map_units_minus1 = ReadExponentialGolombCode();
    int frame_mbs_only_flag = ReadBit();
    if( !frame_mbs_only_flag ) {
        int mb_adaptive_frame_field_flag = ReadBit();
    }
    int direct_8x8_inference_flag = ReadBit();
    int frame_cropping_flag = ReadBit();
    if( frame_cropping_flag ) {
        frame_crop_left_offset = ReadExponentialGolombCode();
        frame_crop_right_offset = ReadExponentialGolombCode();
        frame_crop_top_offset = ReadExponentialGolombCode();
        frame_crop_bottom_offset = ReadExponentialGolombCode();
    }
    int vui_parameters_present_flag = ReadBit();

    *w = ((pic_width_in_mbs_minus1 +1)*16) - frame_crop_bottom_offset*2 - frame_crop_top_offset*2;
    *h = ((2 - frame_mbs_only_flag)* (pic_height_in_map_units_minus1 +1) * 16) - (frame_crop_right_offset * 2) - (frame_crop_left_offset * 2);
    *profile = profile_idc;
    *level = level_idc;

#undef ReadBits
#undef ReadBit
#undef ReadSE
#undef ReadExponentialGolombCode

}

}; // namespace live555

}; // namespace sio