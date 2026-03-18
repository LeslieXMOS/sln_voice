# DFU for FFD

This shows a two firmware approach to perform DFU for FFD application

## Structure

We use example_ffva_ua_adec_altarch.xe as our DFU factory image, example_ffd_cyberon as our application image

## Build

1. cmake -G Ninja -B build --toolchain xmos_cmake_toolchain/xs3a.cmake

2. cd build

3. ninja example_ffva_ua_adec_altarch

4. ninja example_ffd_cyberon

## Prepare upgrade binary and data partition binary

1. xflash --factory-version 15.3 --upgrade 1 .\example_ffd_cyberon.xe -o upgrade_ffd.bin

2. ninja flash_app_example_ffd_cyberon

3. Data partition binary will called example_ffd_cyberon_data_partition.bin inside build folder

## Flash xcore with DFU loader and FFD

1. xflash ./example_ffva_ua_adec_altarch.xe --loader ../loader.o --upgrade 1 ./example_ffd_cyberon.xe 0xAF000 --boot-partition-size 1048576

2. You should see the application keep restarting due to missing cyberon data partition

3. Hold button A and re-power L71

4. dfu-util.exe -l

5. You should see three DFU from L71, called "DFU FACTORY", "DFU UPGRADE", "DFU DATAPARTITION"

6. Update data partition

7. dfu-util.exe -d 20b1:4001 -a 2 -D example_ffd_cyberon_data_partition.bin -R

8. L71 will auto reboot when flash is completed

9. FFD on L71 should working fine now

## Enter DFU mode

1. Long press button A

2. dfu-util.exe -l

3. See if L71 enter DFU mode

## Update through DFU

* Update application
  - dfu-util.exe -d \<pid\>:\<vid\> -a 1 -D \<binary\> -R

* Update data partition
  - dfu-util.exe -d \<pid\>:\<vid\> -a 2 -D \<binary\> -R