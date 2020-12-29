set(SRCS 
    smsRtsp.c
    smsRtspAAC.c
    smsRtspH264.c
    smsRtspHandler.c
    smsRtspPCM.c
    smsRtspSession.c
    smsRtspStream.c
    smsRtspVideoRaw.c
)

add_library(${PROJECT_NAME} SHARED ${SRCS})

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
        DESTINATION bin/${PROJECT_NAME} COMPONENT Runtime
        DESTINATION lib/${PROJECT_NAME} COMPONENT Runtime
        DESTINATION lib/${PROJECT_NAME} COMPONENT Development)

install(EXPORT ${PROJECT_NAME}Targets DESTINATION lib/${PROJECT_NAME})

