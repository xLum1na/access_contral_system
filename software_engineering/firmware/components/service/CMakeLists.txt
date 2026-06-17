idf_component_register(
    SRCS
        "service_db/service_db.c"
        "service_db/service_db_file_io.c"
        "service_db/service_db_index.c"
        "service_db/service_db_user.c"
        "service_db/service_db_face.c"
        "service_db/service_db_log.c"
        "service_face_detect/service_face_detect.cpp"
        "service_face_identify/service_face_identify.cpp"
        "service_user_manage/service_user_manage.c"
    INCLUDE_DIRS
        "service_db/include"
        "service_face_detect/include"
        "service_face_identify/include"
        "service_user_manage/include"
    REQUIRES
        dal osal pal espressif__esp-dl)
