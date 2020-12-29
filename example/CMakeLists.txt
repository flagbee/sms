add_executable(testServer test.c)
target_link_libraries(testServer 
	${PROJECT_NAME}
	pthread
)
