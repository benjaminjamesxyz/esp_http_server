file(GLOB HTTP_SERVER_SRC "src/*.cpp")
idf_component_register(
				SRCS ${HTTP_SERVER_SRC}
				INCLUDE_DIRS "inc"
				REQUIRES
)
