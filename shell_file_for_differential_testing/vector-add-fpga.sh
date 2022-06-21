dpcpp -fintelfpga vector-add/src/vector-add-buffers.cpp -o vector-add/vector-add-buffers.fpga_emu -DFPGA_EMULATOR=1
./vector-add/vector-add-buffers.fpga_emu $1