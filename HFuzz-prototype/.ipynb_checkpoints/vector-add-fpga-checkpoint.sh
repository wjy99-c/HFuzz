dpcpp -fintelfpga ~/A10_oneapi/vector-add/src/vector-add-heterofuzz.cpp -o ~/A10_oneapi/vector-add/vector-add-heterofuzz.fpga_emu -DFPGA_EMULATOR=1
. ~/A10_oneapi/vector-add/vector-add-heterofuzz.fpga_emu $1