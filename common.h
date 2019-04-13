#ifndef __COMMON_H
#define __COMMON_H

#define OK 0

#define ERROR_MAP(XX)											\
	XX(0, OK, "success")										\
	XX(-1, NULL_POINTER, "using NULL pointer")					\
	XX(-2, MUTEX_LOCK, "pthread_mutex_lock error")				\
	XX(-3, MUTEX_UNLOCK, "pthread_mutex_unlock error")			\
	XX(-4, MUTEX_INIT, "pthread_mutex_init error")				\
	XX(-5, MALLOC, "malloc error")								\
	XX(-6, ITEM_UNEXIST, "not such item")						\
	XX(-7, QUEUE_EMPTY, "queue is empty")						\
	XX(-8, JSON_OP, "json error")								\
	XX(1, UNKNOW, "unknow error")

#define ERROR_GEN(val, name, str)	ERR_##name = val,
enum ERR{
	ERROR_MAP(ERROR_GEN)
};
#undef ERROR_GEN

#define ERROR_STR_GEN(val, name, str)	case ERR_##name : return str;
const char *error_code_desc(enum ERR code){
	switch(code){
		ERROR_MAP(ERROR_STR_GEN)
	default:
		return "unknow error";
	}
}
#undef ERROR_STR_GEN

#define ERROR_NAME_GEN(val, name, str)	case ERR_##name : return #name;
const char *error_code_name(enum ERR code){
	switch(code){
		ERROR_MAP(ERROR_NAME_GEN)
	default:
		return "unknow name";
	}
}
#undef ERROR_NAME_GEN

#define API_RET_MAP(XX)												\
	XX(0, SUCCESS, "success")										

#define API_RET_GEN(val, name, str)		API_##name = val,
enum API_RET{
	API_RET_MAP(API_RET_GEN)
};
#undef API_RET_GEN

#define API_STR_GEN(val, name, str)		case API_##name : return str;
const char *api_code_desc(enum API_RET code){
	switch(code){
		API_RET_MAP(API_STR_GEN)
	default:
		return "unknow api ret";
	}
}
#undef API_STR_GEN

#define API_NAME_GEN(val, name, str)	case API_##name : return #name;
const char * api_code_name(enum API_RET code){
	switch(code){
		API_RET_MAP(API_NAME_GEN)
	default:
		return "unknow api ret";
	}
}
#undef API_NAME_GEN

#endif
