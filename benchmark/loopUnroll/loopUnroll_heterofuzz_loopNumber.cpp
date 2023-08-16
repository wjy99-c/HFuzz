//==============================================================
// This sample demonstrates the use of loop unrolling as a simple optimization
// technique to speed up compute and increase memory access throughput.
//==============================================================
// Copyright © Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#include <CL/sycl.hpp>
#include <iomanip>
#include <iostream>
#include <vector>

// dpc_common.hpp can be found in the dev-utilities include folder.
// e.g., $ONEAPI_ROOT/dev-utilities//include/dpc_common.hpp
#include "dpc_common.hpp"
#if FPGA || FPGA_EMULATOR
  #include <sycl/ext/intel/fpga_extensions.hpp>
#endif
using namespace std;
using namespace sycl;

template <int unroll_factor>
class VAdd;

// Adds corresponding elements of two input vectors using a loop. The loop is
// unrolled as many times as specified by the unroll factor.
template <int unroll_factor>
void VectorAdd(queue &q, const vector<float> &a, const vector<float> &b,
               vector<float> &sum) {
  size_t n = a.size();

  buffer buffer_a(a);
  buffer buffer_b(b);
  buffer buffer_sum(sum);

  event e = q.submit([&](handler &h) {
    accessor acc_a(buffer_a, h, read_only);
    accessor acc_b(buffer_b, h, read_only);
    accessor acc_sum(buffer_sum, h, write_only, no_init);

    h.single_task<VAdd<unroll_factor>>([=]()[[intel::kernel_args_restrict]] {
// Unroll loop as specified by the unroll factor.
#pragma unroll unroll_factor
      for (size_t i = 0; i < n; i++) {
        acc_sum[i] = acc_a[i] + acc_b[i];
      }
    });
  });

  double start = e.get_profiling_info<info::event_profiling::command_start>();
  double end = e.get_profiling_info<info::event_profiling::command_end>();

  // Convert from nanoseconds to milliseconds.
  double kernel_time = (end - start) * 1e-6;

  cout << "Unroll factor: " << unroll_factor << " Kernel time: " << kernel_time
       << " ms\n";
  cout << "Throughput for kernel with unroll factor " << unroll_factor << ": ";
  cout << std::fixed << std::setprecision(3) << ((double)n / kernel_time) / 1e6f
       << " GFlops\n";
}

// Initialize vector.
void InitializeVector(vector<float> &a) {
  size_t n = a.size();

  for (size_t i = 0; i < n; i++) {
    a[i] = i;
  }
}

// Verify results.
void VerifyResults(const vector<float> &a, const vector<float> &b,
                   const vector<float> &sum) {
  size_t n = a.size();

  for (size_t i = 0; i < n; i++) {
    if (sum[i] != a[i] + b[i]) {
      cout << "FAILED: The results are incorrect.\n";
    }
  }
}

int main(int argc, char* argv[]) {
  std::string file;
  if (argc > 1) file = argv[1];
  std::ifstream read(file);
    
  if (!read.is_open()){
      std::cout << "Could not open the input file.\n";
  } 
    
  size_t n = 1 << 25;
  read >> n;
  cout << "Input array size: " << n << "\n";
#if FPGA_EMULATOR
  // DPC++ extension: FPGA emulator selector on systems without FPGA card.
  ext::intel::fpga_emulator_selector d_selector;
#elif FPGA
  // DPC++ extension: FPGA selector on systems with FPGA card.
  ext::intel::fpga_selector d_selector;
#else
  // The default device selector will select the most performant device.
  default_selector d_selector;
#endif
  // Input vectors.
  vector<float> a(n);
  vector<float> b(n);
    
  float number;
  int i = 0;
  while ((read >> number) and (i<vector_size)){
    a[i] = number;
    i = i + 1;
  }
  i = 0;
  while ((read >> number) and (i<vector_size)){
    b[i] = number;
    i = i + 1;
  }
  read.close();
  // Output vector.
  vector<float> sum(n);

  try {
    queue q(d_selector, dpc_common::exception_handler,
            property::queue::enable_profiling{});

    cout << "Running on device: "
         << q.get_device().get_info<info::device::name>() << "\n";

    // Instantiate VectorAdd kernel with different unroll factors: 1, 2, 4,
    // 8, 16. The VectorAdd kernel contains a loop that adds corresponding
    // elements of two input vectors. That loop is unrolled by the specified
    // unroll factor.
    VectorAdd<1>(q, a, b, sum);
    VerifyResults(a, b, sum);
    VectorAdd<2>(q, a, b, sum);
    VerifyResults(a, b, sum);
    VectorAdd<4>(q, a, b, sum);
    VerifyResults(a, b, sum);
    VectorAdd<8>(q, a, b, sum);
    VerifyResults(a, b, sum);
    VectorAdd<16>(q, a, b, sum);
    VerifyResults(a, b, sum);

  } catch (sycl::exception const &e) {
    cerr << "SYCL host exception:\n" << e.what() << "\n";
    terminate();
  }
  std::ofstream outfile;
  outfile.open("exec_fpga_info.txt");
  outfile << n/16 << std::endl <<  n << std::endl << n mod 16;
  outfile.close();
    
  cout << "PASSED: The results are correct.\n";
  return 0;
}
