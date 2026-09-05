cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES CXX)

enable_testing()

add_executable(motion_math_test
    motion_math_test.cpp
    ../main/stackchan/motion/motion_math.cpp
)

target_include_directories(motion_math_test PRIVATE
    ../main
)

target_compile_features(motion_math_test PRIVATE cxx_std_17)

add_test(NAME motion_math_test COMMAND $<TARGET_FILE:motion_math_test>)

add_executable(media_track_identity_test
    media_track_identity_test.cpp
)

target_include_directories(media_track_identity_test PRIVATE
    ../main
)

target_compile_features(media_track_identity_test PRIVATE cxx_std_17)

add_test(NAME media_track_identity_test COMMAND $<TARGET_FILE:media_track_identity_test>)

add_executable(pmic_power_state_test
    pmic_power_state_test.cpp
)

target_include_directories(pmic_power_state_test PRIVATE
    ../main
)

target_compile_features(pmic_power_state_test PRIVATE cxx_std_17)

add_test(NAME pmic_power_state_test COMMAND $<TARGET_FILE:pmic_power_state_test>)

add_executable(screen_swipe_classifier_test
    screen_swipe_classifier_test.cpp
)

target_include_directories(screen_swipe_classifier_test PRIVATE
    ../main
)

target_compile_features(screen_swipe_classifier_test PRIVATE cxx_std_17)

add_test(NAME screen_swipe_classifier_test COMMAND $<TARGET_FILE:screen_swipe_classifier_test>)

add_executable(usb_speaker_pcm_test
    usb_speaker_pcm_test.cpp
)

target_include_directories(usb_speaker_pcm_test PRIVATE
    ../main
)

target_compile_features(usb_speaker_pcm_test PRIVATE cxx_std_17)

add_test(NAME usb_speaker_pcm_test COMMAND $<TARGET_FILE:usb_speaker_pcm_test>)
