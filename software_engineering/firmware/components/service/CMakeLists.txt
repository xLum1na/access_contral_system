idf_component_register(
    SRCS
        "service_db/service_db.c"
        "service_db/service_db_file_io.c"
        "service_db/service_db_index.c"
        "service_db/service_db_user.c"
        "service_db/service_db_face.c"
        "service_db/service_db_log.c"
    INCLUDE_DIRS
        "service_db/include"
    REQUIRES
        dal osal pal)
