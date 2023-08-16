 //==============================================================
// Copyright © 2020 Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#include "GSimulation.hpp"

// dpc_common.hpp can be found in the dev-utilities include folder.
// e.g., $ONEAPI_ROOT/dev-utilities/latest/include/dpc_common.hpp
#include "dpc_common.hpp"
#include "FakeIOPipes.hpp"
#include "HostSideChannel.hpp"
#if FPGA || FPGA_EMULATOR
  #include <sycl/ext/intel/fpga_extensions.hpp>
#endif

struct DeviceToHostSideChannelID;
struct SideChannelMainKernel;
using MyDeviceToHostSideChannel = DeviceToHostSideChannel<DeviceToHostSideChannelID, RealType, true, 512>;

using namespace sycl;

    // Create device selector for the device of your interest.
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

/* Default Constructor for the GSimulation class which sets up the default
 * values for number of particles, number of integration steps, time steo and
 * sample frequency */
GSimulation::GSimulation() {
  std::cout << "==============================="
            << "\n";
  std::cout << " Initialize Gravity Simulation"
            << "\n";
  set_npart(16000);
  set_nsteps(10);
  set_tstep(0.1);
  set_sfreq(1);
}

/* Set the number of particles */
void GSimulation::SetNumberOfParticles(int N) { set_npart(N); }

/* Set the number of integration steps */
void GSimulation::SetNumberOfSteps(int N) { set_nsteps(N); }

/* Initialize the position of all the particles using random number generator
 * between 0 and 1.0 */
void GSimulation::InitPos(std::string file) {
  
  std::ifstream read(file);
  if (!read.is_open()){
      std::cout << "Could not open the input file.\n";
  } 
    
  std::random_device rd;  // random number generator
  std::mt19937 gen(42);
  std::uniform_real_distribution<RealType> unif_d(0, 1.0);

  for (int i = 0; i < get_npart(); ++i) {
    float number;
    particles_[i].pos[0] = unif_d(gen);
    particles_[i].pos[1] = unif_d(gen);
    particles_[i].pos[2] = unif_d(gen);
    if (read >> number){
        particles_[i].pos[0] = number;
    }
    if (read >> number){
        particles_[i].pos[1] = number;
    }
    if (read >> number){
        particles_[i].pos[2] = number;
    }
  }
}

/* Initialize the velocity of all the particles using random number generator
 * between -1.0 and 1.0 */
void GSimulation::InitVel() {
  std::random_device rd;  // random number generator
  std::mt19937 gen(42);
  std::uniform_real_distribution<RealType> unif_d(-1.0, 1.0);

  for (int i = 0; i < get_npart(); ++i) {
    particles_[i].vel[0] = unif_d(gen) * 1.0e-3f;
    particles_[i].vel[1] = unif_d(gen) * 1.0e-3f;
    particles_[i].vel[2] = unif_d(gen) * 1.0e-3f;
  }
}

/* Initialize the acceleration of all the particles to 0 */
void GSimulation::InitAcc() {
  for (int i = 0; i < get_npart(); ++i) {
    particles_[i].acc[0] = 0.f;
    particles_[i].acc[1] = 0.f;
    particles_[i].acc[2] = 0.f;
  }
}


/* Initialize the mass of all the particles using a random number generator
 * between 0 and 1 */
void GSimulation::InitMass() {
  RealType n = static_cast<RealType>(get_npart());
  std::random_device rd;  // random number generator
  std::mt19937 gen(42);
  std::uniform_real_distribution<RealType> unif_d(0.0, 1.0);

  for (int i = 0; i < get_npart(); ++i) {
    particles_[i].mass = n * unif_d(gen);
  }
}

/* This function does the simulation logic for Nbody */
void GSimulation::Start(std::string file) {
  RealType dt = get_tstep();
  int n = get_npart();
  particles_.resize(n);
  
  
  InitPos(file);
  InitVel();
  InitAcc();
  InitMass();

  PrintHeader();

  total_time_ = 0.;

  float kSofteningSquared = 1e-14f;
  // prevents explosion in the case the particles are really close to each other
  float kG = 6.67259e-11f;
  double gflops = 1e-9 * ((11. + 18.) * n * n + n * 19.);
  int nf = 0;
  double av = 0.0, dev = 0.0;
  // Create global range
  auto r = range<1>(n);
  // Create local range
  auto lr = range<1>(128);
  // Create ndrange 
  auto ndrange = nd_range<1>(r, lr);
  // Create a queue to the selected device and enabled asynchronous exception
  // handling for that queue
  queue q(d_selector, dpc_common::exception_handler);
  // Create SYCL buffer for the Particle array of size "n"
    buffer pbuf(particles_.data(), r,
              {cl::sycl::property::buffer::use_host_ptr()});
  // Allocate energy using USM allocator shared
  RealType *energy = malloc_shared<RealType>(1,q);
  *energy = 0.f;

  float acc_max = 0;
  float acc_min = 0;
  MyDeviceToHostSideChannel::Init(q);
  dpc_common::TimeInterval t0;
  int nsteps = get_nsteps();
  // Looping across integration steps
  for (int s = 1; s <= nsteps; ++s) {
    
    dpc_common::TimeInterval ts0;
    // Submitting first kernel to device which computes acceleration of all
    // particles
     q.submit([&](handler& h) {
       auto p = pbuf.get_access(h);
       h.parallel_for(ndrange, [=](nd_item<1> it) {
	 auto i = it.get_global_id();
         RealType acc_max = 0;
         RealType acc_min = 0;
         RealType acc0 = 0;
         RealType acc1 = 0;
         RealType acc2 = 0;
         for (int j = 0; j < n; j++) {
           RealType dx, dy, dz;
           RealType distance_sqr = 0.0f;
           RealType distance_inv = 0.0f;

           dx = p[j].pos[0] - p[i].pos[0];  // 1flop
           dy = p[j].pos[1] - p[i].pos[1];  // 1flop
           dz = p[j].pos[2] - p[i].pos[2];  // 1flop

           distance_sqr =
               dx * dx + dy * dy + dz * dz+ kSofteningSquared;  // 6flops
           distance_inv = 1.0f / sycl::sqrt(distance_sqr);       // 1div+1sqrt

           if (dx==0) {acc0=0;}
           else{
             acc0 += dx * kG * p[j].mass * distance_inv * distance_inv *
                   distance_inv;  // 6flops
             
           }
           if (dy==0) {acc1=0;}
           else{
           acc1 += dy * kG * p[j].mass * distance_inv * distance_inv *
                   distance_inv;  // 6flops
           }
           if (dz==0){acc2=0;}
           else{
           acc2 += dz * kG * p[j].mass * distance_inv * distance_inv *
                   distance_inv;  // 6flops
           }
           if (acc1>acc_max) {acc_max=acc1;}
           if (acc2>acc_max) {acc_max=acc2;}
           if (acc0>acc_max) {acc_max=acc0;}
             
           if (acc1<acc_min) {acc_min=acc1;}
           if (acc2<acc_min) {acc_min=acc2;}
           if (acc0<acc_min) {acc_min=acc0;}
         }
         p[i].acc[0] = acc0;
         p[i].acc[1] = acc1;
         p[i].acc[2] = acc2;
           
         p[i].vel[0] += acc0 * dt;  // 2flops
         p[i].vel[1] += acc1 * dt;  // 2flops
         p[i].vel[2] += acc2 * dt;  // 2flops
         bool flag=true;
         if (-acc_min>acc_max){MyDeviceToHostSideChannel::write(-acc_min,flag);}
         else {MyDeviceToHostSideChannel::write(acc_max,flag);}
       });
     }).wait_and_throw();
    // Second kernel updates the velocity and position for all particles
      
    q.submit([&](handler& h) {
       auto p = pbuf.get_access(h);
       h.parallel_for(ndrange, [=](nd_item<1> it) {
	    auto i = it.get_global_id();

         p[i].pos[0] += p[i].vel[0] * dt;  // 2flops
        p[i].pos[1] += p[i].vel[1] * dt;  // 2flops
         p[i].pos[2] += p[i].vel[2] * dt;  // 2flops

         energy[0] += (p[i].mass *
                (p[i].vel[0] * p[i].vel[0] + p[i].vel[1] * p[i].vel[1] +
                 p[i].vel[2] * p[i].vel[2]));  // 7flops
       });
     }).wait_and_throw();
      
    kenergy_ = 0.5 * (*energy);
    *energy = 0.f;
    double elapsed_seconds = ts0.Elapsed();
    if ((s % get_sfreq()) == 0) {
      nf += 1;
      std::cout << " " << std::left << std::setw(8) << s << std::left
                << std::setprecision(5) << std::setw(8) << s * get_tstep()
                << std::left << std::setprecision(5) << std::setw(12)
                << kenergy_ << std::left << std::setprecision(5)
                << std::setw(12) << elapsed_seconds << std::left
                << std::setprecision(5) << std::setw(12)
                << gflops * get_sfreq() / elapsed_seconds << "\n";
      if (nf > 2) {
        av += gflops * get_sfreq() / elapsed_seconds;
        dev += gflops * get_sfreq() * gflops * get_sfreq() /
               (elapsed_seconds * elapsed_seconds);
      }
    }

    for (int i=0; i<n; i++){
      bool flag;
      auto acpt = MyDeviceToHostSideChannel::read(flag);
        if (particles_[i].acc[0]>acc_max) {acc_max=particles_[i].acc[0];}
        if (particles_[i].acc[1]>acc_max) {acc_max=particles_[i].acc[1];}
        if (particles_[i].acc[2]>acc_max) {acc_max=particles_[i].acc[2];}
             
        if (particles_[i].acc[0]<acc_min) {acc_min=particles_[i].acc[0];}
        if (particles_[i].acc[1]<acc_min) {acc_min=particles_[i].acc[1];}
        if (particles_[i].acc[2]<acc_min) {acc_min=particles_[i].acc[2];}
    }
    if (-acc_min>acc_max) {acc_max = -acc_min;}
  }  // end of the time step loop
  total_time_ = t0.Elapsed();
  total_flops_ = gflops * get_nsteps();
  av /= (double)(nf - 2);
  dev = sqrt(dev / (double)(nf - 2) - av * av);
  MyDeviceToHostSideChannel::Destroy(q);
  std::cout <<"\n";
  std::cout << "# Total Time (s)     : " << total_time_ << "\n";
  std::cout << "# Average Performance : " << av << " +- " << dev << "\n";
  std::cout << "==============================="
            << "\n";
  std::ofstream outfile;
  outfile.open("exec_fpga_info.txt");
  outfile << acc_max << std::endl << acc_max << std::endl;
  outfile.close();
}

/* Print the headers for the output */
void GSimulation::PrintHeader() {
  std::cout << " nPart = " << get_npart() << "; "
            << "nSteps = " << get_nsteps() << "; "
            << "dt = " << get_tstep() << "\n";

  std::cout << "------------------------------------------------"
            << "\n";
  std::cout << " " << std::left << std::setw(8) << "s" << std::left
            << std::setw(8) << "dt" << std::left << std::setw(12) << "kenergy"
            << std::left << std::setw(12) << "time (s)" << std::left
            << std::setw(12) << "GFLOPS"
            << "\n";
  std::cout << "------------------------------------------------"
            << "\n";
}
