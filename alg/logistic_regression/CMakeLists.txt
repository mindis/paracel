add_library(lg_update SHARED update.cpp)
target_link_libraries(lg_update ${CMAKE_DL_LIBS})
install(TARGETS lg_update LIBRARY DESTINATION lib)

add_library(lg_method SHARED lg.cpp)
target_link_libraries(lg_method ${Boost_LIBRARIES})
install(TARGETS lg_method LIBRARY DESTINATION lib)

add_library(lg_ssp_method SHARED lg_ssp.cpp)
target_link_libraries(lg_ssp_method ${Boost_LIBRARIES})
install(TARGETS lg_ssp_method LIBRARY DESTINATION lib)

add_executable(lg lg_driver.cpp)
target_link_libraries(lg
  ${Boost_LIBRARIES} 
  comm scheduler lg_ssp_method)
install(TARGETS lg RUNTIME DESTINATION bin)

#add_executable(lr_l1_serial lr_l1_serial.cpp)
#target_link_libraries(lr_l1_serial ${Boost_LIBRARIES} comm scheduler)
#install(TARGETS lr_l1_serial RUNTIME DESTINATION bin)
