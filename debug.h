#ifndef JO_DEBUG_H
#define JO_DEBUG_H
#define ERR_LOG(fmt, arg...) fprintf(stderr, fmt, ##arg)
#define FILE_LOG(file, fmt, arg...) fprintf(file, fmt, ##arg)
#endif
