# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS 
	"main.c" 
	"app_camera.c"
	"app_mdns.c"
	"app_httpd.c" 	
)
set(COMPONENT_ADD_INCLUDEDIRS 
	"include"
)

register_component()