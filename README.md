# HeteroTest

## 1 Introduction

This is the repo for the summer task. We need to build a cross-devices differential testing tool that works on devcloud with DPC++.

## 2 [Getting account on devcloud](https://github.com/intel/FPGA-Devcloud/blob/master/main/Devcloud_Access_Instructions/README.md)

Since we run the experiment on devcloud, pls register an account [here](https://devcloud.intel.com/oneapi/get_started/). It is totally free.

Once you've signed up, you should get an immidate screen response with your new user ID and instructions on how to set up your account. You will also receive a follow up email from Intel Devcloud which can take **1 hr** giving you a record of your user ID and your user ID key for login. This is an example of the resulting email which will be sent to you:

```
Welcome "user name",

We are excited that you chose Intel® FPGA Cloud.  Free access. No downloads. No installations. No maintenance. 

Your account should already be activated. Below are your credentials for your reference. 
Unique Access URL: https://devcloud.intel.com/fpga/?uuid=cd1d...

User ID: u12345
UUID Key: cd1d...

Access to Intel® FPGA DevCloud typically expires after 120 days. However, longer access times are granted based on user request. To extend this access, please email fpgauniversity@intel.com and give the extension time needed to complete your project.

Getting started with FPGA Cloud:
1. Access detailed instructions for environment setup: https://github.com/intel/FPGA-Devcloud/tree/master/main/Devcloud_Access_Instructions#devcloud-access-instructions
2. If you have technical questions or recommendations, please post them to our FPGA forum: https://forums.intel.com/s/topic/0TO0P000000MWKFWA4/application-acceleration-with-fpgas
3. If you have unresolved issues, email fpgauniversity@intel.com and give us a detailed description of your problem.
It's all about you and your code. We look forward to the innovations you'll create.

- Your friendly Intel DevCloud Team 
```

Pls follow the steps on devcloud readme.md file until you can succesfully [login the oneapi nodes](https://github.com/intel/FPGA-Devcloud/blob/master/main/Devcloud_Access_Instructions/README.md#50-connecting-to-servers-running-fpga-development-software)

## 3 Prepare: HeteroFuzz

Download the repo on your devcloud account while on login-2 mode and cd into heterofuzz folder:
```
user@iMac:~$ ssh devcloud
u12345@login-2:~$ git clone https://github.com/wjy99-c/HeteroTest.git
u12345@login-2:~$ cd HeteroTest/HeteroFuzz/prototype; make
```
It is a downgraded version of HeteroFuzz. The hardware monitor is turned off so it should works similar to AFL. This HeteroFuzz will be our baseline. Follow the steps on HeteroFuzz [readme](https://github.com/UCLA-SEAL/HeteroFuzz/blob/main/readme.md). Try to run hello world example in HeteroFuzz with:

```
./fuzz good-seeds/ good-outputs/ 10 hello
```

## 4 Prepare: Vector-add

Login in one of the oneapi node and go to vector-add folder:

```
cd ~/HeteroTest/vector-add
```
Under src folder, there are two versions of vector-add. Pls first try vector-add-buffers.cpp first. Complie the initial version with dpc++ compiler and run the emulation version:

```
dpcpp -fintelfpga src/vector-add-buffers.cpp -o vector-add-buffers.fpga_emu -DFPGA_EMULATOR=1
./vector-add-buffers.fpga_emu
```
You should be able to see the success notification at end:
```
Vector add successfully completed on device.
```

## 5 Use Heterofuzz for vector-add:

Since the heterofuzz we used is a downgraded version, there is no fpga specific feedback for test generation. To enable fuzzing, we should modify the vector-add with a few lines of code. I already prepared a modified version as vector-add-heterofuzz.cpp:
```
dpcpp -fintelfpga src/vector-add-heterofuzz.cpp -o vector-add-heterofuzz.fpga_emu -DFPGA_EMULATOR=1
./vector-add-heterofuzz.fpga_emu your_test_file
```
Run heterofuzz for vector-add:
```
../HeteroFuzz/prototype/fuzz your_input_file_folder your_good_outputs_folder 10 vector-add-heterofuzz.fpga_emu 
```
Since there is no feedback from the vector-add, your_good_outputs_folder should only have the seed input.
