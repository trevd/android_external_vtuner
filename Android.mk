LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE        := vtunerd
LOCAL_MODULE_TAGS   := optional
LOCAL_SRC_FILES     := vtunerd.c vtunerd-dvb.c vtunerd-service.c vtuner-network.c
LOCAL_SHARED_LIBRARIES += liblog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE        := vtunerd
LOCAL_MODULE_TAGS   := optional
LOCAL_SRC_FILES     := vtunerd.c vtunerd-dvb.c vtunerd-service.c vtuner-network.c
LOCAL_SHARED_LIBRARIES += liblog
LOCAL_LDFLAGS := -lrt
include $(BUILD_HOST_EXECUTABLE)


include $(CLEAR_VARS)
LOCAL_MODULE        := vtunerc
LOCAL_MODULE_TAGS   := optional
LOCAL_SRC_FILES     := vtunerc.c vtuner-network.c	
LOCAL_SHARED_LIBRARIES += liblog
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE        := get_fe_info
LOCAL_MODULE_TAGS   := optional
LOCAL_SRC_FILES     := tools/get_fe_info.c
include $(BUILD_EXECUTABLE)



