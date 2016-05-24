#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <semaphore.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sched.h>
#include <pthread.h>
#include <fcntl.h>
#include <math.h>

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavutil/parseutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "list.h"
#include "contracts.h"

//#define FPS_HISTORY_COUNT 32
#define TEST_DECODE_OUTPUT
#define AUTOMATIC_ALLOC_THREAD
//#define DEC_THREAD_SCHEDULING
//#define ENC_THREAD_SCHEDULING


typedef struct EncodeStruct {
	int full_gop_buffer_id;
	int dst_width;
	int dst_height;
	int bitrate;
	enum AVPixelFormat dst_pix_fmt;
	char *output_filename;
	int thread_num;
	char output_dir[256];
	int running_thread;
	int encoded_frame_cnt;
	sem_t running_thread_mutex;
}EncodeStruct;

typedef struct Period{
	long start;
	long end;
}Period;

static const char *SRC_FILENAME = NULL;

static enum AVPixelFormat SRC_PIX_FMT;
static int SRC_WIDTH, SRC_HEIGHT;
static int SRC_PICTURE_SIZE = 0;
static int REFCOUNT = 0;
static int START_PTS = -1;
static int DECODE_STEP = 300;
static int PTS_DURATION = -1;
static int TICKS_PER_FRAME;
static int HAS_AUDIO = 0;
AVRational CODEC_TIME_BASE;
AVRational STREAM_TIME_BASE;
AVRational SAMPLE_ASPECT_RATIO;

static int VIDEO_FRAME_COUNT = 0;
static int NEXT_BINDING_CORE_ID = 0;
//static float FPS_HISTORY[FPS_HISTORY_COUNT];

static List *PERIOD_LIST = NULL;
static float AVERAGE_FPS = 0;
static int FPS_COUNT = 0;

static int THREAD_COUNT = 0;
static int DEC_THREAD_CNT  = 0;
static int ENC_THREAD_CNT = 0;
static int THREAD_CNT = 0;

static int GOP_SIZE = 100;
static int GOP_COUNT = 0;

static int OUTPUT_VIDEO_NUM = 0;   
static int ID_OF_THE_FIRST_CORE_TO_BIND = 0;
static int FULL_GOP_BUFFER_SIZE = 80;

static int CONSOL_OUTPUT = 1;
static const int FPS_MEASURE_DURATION = 1;
static int DECODED_FRAMES = 0;
static int PRINT_DYNAMIC_FPS = 0;
static int BUSY_ENCODE_THREAD_COUNT = 0;

static int num_cores;

static EncodeStruct ess[6] =
{
	{0, 1920, 1080, 8000000, AV_PIX_FMT_YUV420P, "", 1, "", 0, 0},
	{1, 1920, 1080, 4000000, AV_PIX_FMT_YUV420P, "", 1,"", 0, 0},
	{2, 1280, 720, 2300000, AV_PIX_FMT_YUV420P, "", 1,"", 0, 0},
	{3, 1280, 720, 1300000, AV_PIX_FMT_YUV420P, "", 1, "", 0, 0},
	{4, 640, 360, 800000, AV_PIX_FMT_YUV420P, "", 1,"",  0, 0},
	{5, 640, 360, 600000, AV_PIX_FMT_YUV420P, "", 1, "", 0, 0}
};

sem_t PERIOD_LIST_MUTEX;
sem_t VIDEO_FRAME_COUNT_MUTEX;
sem_t get_core_id_mutex;
sem_t ALIVED_DEC_THREAD_CNT;


// data related to encodng threads transfer between different encoding tasks

// each time only one thread transfered from one encoding task to another
sem_t HELP_WITH_OTHER_TASK;
// each time a encodnig thread encoded CHECKING_FREQ GOPs, it would
// check whether it should help with other encoding tasks
//int CHECKING_FREQ = 5;
int CHECKING_FREQ = 3;
// only when the fastest encoding task faster than the slowest encoding task
// FASTER_IN_GOP GOPs will it help with the slowest task
//int FASTER_IN_GOP = 5;
int FASTER_IN_GOP = 3;

// data related to decoding threads transfered to encoding tasks

// each time a decoding thread decoded (CHECKING_FREQ_OF_DEC_THREAD * GOP_SIZE) frames,
// it would check whether it should help with the slowest encoding task
//int CHECKING_FREQ_OF_DEC_THREAD = 5;
int CHECKING_FREQ_OF_DEC_THREAD = 3;
// only when the slowest encoding task has more than DEC_THREAD_TRANSFER_COND,
// will the decoding thread help with it
//int DEC_THREAD_TRANSFER_COND = 20; // set to 2 * (CHECKING_FREQ_OF_ENC_THREAD + FASTER_IN_GOP)
int DEC_THREAD_TRANSFER_COND = 10;

void core_binding_initialize() {
	sem_init(&get_core_id_mutex, 0, 1);
	num_cores = sysconf(_SC_NPROCESSORS_CONF);
	NEXT_BINDING_CORE_ID = ID_OF_THE_FIRST_CORE_TO_BIND;
}

int get_core_id() {
	sem_wait(&get_core_id_mutex);
	int id = NEXT_BINDING_CORE_ID;
	NEXT_BINDING_CORE_ID += 1;
	sem_post(&get_core_id_mutex);
	return id % num_cores;
}

void binding_core() {
	int core_id = get_core_id();
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core_id, &mask);
	if (pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0) {
		fprintf(stderr, "core id = %d, thread set affinity failed\n", core_id);
	}
}

/*
void print_average_fps(){
	int i;
	float encoded_frame_cnt = 0;
	float decoded_frame_cnt = 0;
	sem_wait(&VIDEO_FRAME_COUNT_MUTEX);
	decoded_frame_cnt = DECODED_FRAMES;
	DECODED_FRAMES = 0;
	sem_post(&VIDEO_FRAME_COUNT_MUTEX);
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) sem_wait(&ess[i].running_thread_mutex);
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) {
		encoded_frame_cnt += ess[i].encoded_frame_cnt;
		ess[i].encoded_frame_cnt = 0;
	}
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) sem_post(&ess[i].running_thread_mutex);

	if(PRINT_DYNAMIC_FPS){
		float decode_fps =  decoded_frame_cnt / FPS_MEASURE_DURATION;
		float encode_fps =  encoded_frame_cnt / OUTPUT_VIDEO_NUM / FPS_MEASURE_DURATION;
		int min_index = 0;
		for(i = 0; i < FPS_HISTORY_COUNT; ++i){
			if(FPS_HISTORY[min_index] > FPS_HISTORY[i]) min_index = i;
		}
		if( FPS_HISTORY[min_index] < encode_fps || abs(FPS_HISTORY[min_index] - encode_fps) < (FPS_HISTORY[min_index] * 0.3f)){
			srand(time(0));
			int index = rand() % FPS_HISTORY_COUNT;
			FPS_HISTORY[index] = encode_fps;
		}
		float average_fps = 0;
		int count = 0;
		for(i = 0; i < FPS_HISTORY_COUNT; ++i){
			if(FPS_HISTORY[i] > 0) ++count;
			average_fps += FPS_HISTORY[i];
		}

		if(count > 0)
			average_fps /= count;
		AVERAGE_FPS += average_fps;
		++FPS_COUNT;
		printf("Decode FPS: %.2f \t Encode FPS: %.2f \t FPS: %.2f  \n", decode_fps, encode_fps, average_fps);
	}
}

*/

void print_average_fps(){
	int i;
	float encoded_frame_cnt = 0;
	float decoded_frame_cnt = 0;
	sem_wait(&VIDEO_FRAME_COUNT_MUTEX);
	decoded_frame_cnt = DECODED_FRAMES;
	DECODED_FRAMES = 0;
	sem_post(&VIDEO_FRAME_COUNT_MUTEX);
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) sem_wait(&ess[i].running_thread_mutex);
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) {
		encoded_frame_cnt += ess[i].encoded_frame_cnt;
		ess[i].encoded_frame_cnt = 0;
	}
	for(i = 0; i < OUTPUT_VIDEO_NUM; ++i) sem_post(&ess[i].running_thread_mutex);

	if(PRINT_DYNAMIC_FPS){
		float decode_fps =  decoded_frame_cnt / FPS_MEASURE_DURATION;
		float encode_fps =  encoded_frame_cnt / OUTPUT_VIDEO_NUM / FPS_MEASURE_DURATION;

		if(encode_fps > AVERAGE_FPS || abs(AVERAGE_FPS - encode_fps) < AVERAGE_FPS * 0.2){
			AVERAGE_FPS += encode_fps;
			++FPS_COUNT;
		}

		printf("Decode FPS: %.2f \t Encode FPS: %.2f \n", decode_fps, encode_fps);
	}
}


// time record related function
// struct used to record time cost
struct time_record {
	struct timeval start;
	struct timeval end;
	unsigned long time_cost; // time cost of a function in us
	char* function_name;
};
typedef struct time_record * TimeRecord;

TimeRecord decode_time_cost;
TimeRecord scale_time_cost;
TimeRecord encode_time_cost;
TimeRecord output_packets_time_cost;

char* clone_string(char* s) {
	char * clone = (char*)malloc(strlen(s) + 1);
	sprintf(clone, "%s", s);
	return clone;
}

TimeRecord time_record_new(char* function_name) {
	TimeRecord R = (TimeRecord) malloc(sizeof(struct time_record));
	R->time_cost = 0;
	R->function_name = clone_string(function_name);
	return R;
}

void time_record_interval_start(TimeRecord T) {
	gettimeofday(&T->start, NULL);
}

void time_record_interval_end(TimeRecord T) {
	gettimeofday(&T->end, NULL);
}

// RETURNS: the time past in us between start and end
inline static unsigned long timeval_difference(struct timeval start, struct timeval end) {
	return (end.tv_sec - start.tv_sec) * 1000 * 1000
		+ (end.tv_usec - start.tv_usec);
}

// EFFECT: add the time passed between T->start and T->end to T->time_cost
void time_record_interval_add(TimeRecord T) {
	T->time_cost +=
		timeval_difference(T->start, T->end);
}

void time_record_print(TimeRecord T) {
	printf("Time cost of %s is %lu ms\n", T->function_name,
			T->time_cost / 1000);
}

void time_record_free(TimeRecord T) {
	if (T != NULL) {
		free(T->function_name);
		free(T);
	}
}

void initialize_time_records() {
	decode_time_cost = time_record_new("decode time cost");
	scale_time_cost = time_record_new("scale time cost");
	encode_time_cost = time_record_new("encode time cost ");
	return;
}

void print_time_records() {
	time_record_print(decode_time_cost);
	time_record_print(scale_time_cost);
	time_record_print(encode_time_cost);
	time_record_print(output_packets_time_cost);
	return;
}

void free_time_records() {
	time_record_free(decode_time_cost);
	time_record_free(scale_time_cost);
	time_record_free(encode_time_cost);
	time_record_free(output_packets_time_cost);
	return;
}

static int open_codec_context(int *stream_idx,
		AVFormatContext *fmt_ctx, enum AVMediaType type)
{
	int ret, stream_index;
	AVStream *st;
	AVCodecContext *dec_ctx = NULL;
	AVCodec *dec = NULL;
	AVDictionary *opts = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
				av_get_media_type_string(type), SRC_FILENAME);
		return ret;
	} else {
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		dec_ctx = st->codec;
		dec = avcodec_find_decoder(dec_ctx->codec_id);
		if (!dec) {
			fprintf(stderr, "Failed to find %s codec\n",
					av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		dec_ctx->thread_count = 1;

		/* Init the decoders, with or without reference counting */
		av_dict_set(&opts, "refcounted_frames", REFCOUNT ? "1" : "0", 0);
		if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
					av_get_media_type_string(type));
			return ret;
		}
		*stream_idx = stream_index;
	}
	return 0;
}

int flush_encoder(AVFormatContext *fmt_ctx,unsigned int stream_index){
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
				CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2 (fmt_ctx->streams[stream_index]->codec, &enc_pkt,
				NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame){
			ret=0;
			break;
		}

		ret = av_write_frame(fmt_ctx, &enc_pkt);

		if (ret < 0)
			break;
	}
	return ret;
}


/***************************Decode related functions**************************/
List *GOP_BUFFER = NULL;  // contains the decoded and scaled gop frames
sem_t GOP_BUFFER_MUTEX;

#include "patch_queue.h"

#define FULL_GOP_BUFFER_INITIAL_SIZE 50

queue **FULL_GOP_BUFFER = NULL;    // contains processed gops where each gop contains GOP_SIZE elems
//sem_t *FULL_GOP_BUFFER_MUTEX;     // lock each FULL_GOP_BUFFER access
//sem_t *FULL_GOP_NUM;              // count number of gop in each list of FULL_GOP_BUFFER
//sem_t COUNT_FULL_GOP_BUFFER_REF_GOP;  // guess: the free blocks in FULL_GOP_BUFFER

typedef struct Gop {
	int gop_id;
	int frame_cnt;   // the number of frames that actually exists
	int capacity;
	uint8_t *buf;
	uint8_t *valid;
	sem_t gop_mutex;
	int encoded_cnt; // how many times frames in this gop have been encoded
}Gop;

#include "patch_memory.h"
ss_memory_t gop_structure_memory;
ss_memory_t gop_buffer_memory;

Gop *new_Gop(int gop_id, int gop_size) {
//	fprintf(stdout, "new Gop is evoked, gop structure size: %lu gop size: %d\n", sizeof(Gop), SRC_PICTURE_SIZE * gop_size);
	Gop *g = (Gop *)ss_malloc(&gop_structure_memory, sizeof(Gop));
	g->gop_id = gop_id;
	g->frame_cnt = 0;
	g->capacity = gop_size;
	g->buf = (uint8_t *)ss_malloc(&gop_buffer_memory, SRC_PICTURE_SIZE * gop_size);
	g->valid = xcalloc(gop_size, sizeof(uint8_t));
	g->encoded_cnt = 0;
	sem_init(&(g->gop_mutex), 0, 1);
	return g;
}

// EFFECT: free the given Gop struct and its buffer iff its frames have been
// encoded OUTPUT_VIDEO_NUM times
void free_Gop(Gop *g) {
//	fprintf(stdout, "free Gop is evoked\n");
	sem_wait(&g->gop_mutex);
	g->encoded_cnt++;
	if (g->encoded_cnt == OUTPUT_VIDEO_NUM) {
		ss_free(&gop_buffer_memory, g->buf);
		ss_free(&gop_structure_memory, g);
	}
	sem_post(&g->gop_mutex);
	return;
}

bool is_empty_Gop(Gop *g) {
	return (g->frame_cnt == 0);
}


static inline bool is_i_frame(AVFrame *frame) {
	return frame->pict_type == AV_PICTURE_TYPE_I;
}

static inline bool is_key_frame(AVFrame *frame) {
	return frame->key_frame == 1;
}

// RETURNS: the string representation of the frame;
static inline char* get_frame_type(AVFrame *frame) {
	switch (frame->pict_type) {
		case AV_PICTURE_TYPE_I:
			return "I";
		case AV_PICTURE_TYPE_P:
			return "P";
		case AV_PICTURE_TYPE_B:
			return "B";
		default:
			return "Not I/P/B";
	}
}


// GIVEN: the gop_id of a gop
// RETURNS: the corresponding gop in gop_buffer if exists, otherwise returns
Gop *find_gop_in_gop_buffer(int gop_id) {
	if (is_empty_List(GOP_BUFFER)) {
		return NULL;
	} else {
		ListNode *curr = GOP_BUFFER->start;
		while(true) {
			if (((Gop *)curr->elem)->gop_id == gop_id) {
				return (Gop *)curr->elem;
			} else {
				if (curr == GOP_BUFFER->end) {
					return NULL;
				} else {
					curr = curr->next;
				}
			}
		}
	}
}

// EFFECT: add the given gop to GOP_BUFFER
void add_gop_to_gop_buffer(Gop *g) {
	ListNode * node = new_ListNode(g);
	insert_List(node, GOP_BUFFER);
	return;
}

// WHERE: GOP_BUFFER contains g
// EFFECT: remove the given gop from GOP_BUFFER
void remove_gop_from_gop_buffer(Gop *g) {
	ENSURES(g != NULL);
	ENSURES(!is_empty_List(GOP_BUFFER));
	remove_elem_List(GOP_BUFFER, g);
}

// RETURNS: the number of different gops in all FULL_GOP_BUFFER
int count_gop_in_full_gop_buffer() {
	int count = 0;
	int i;
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		queue *buffer = FULL_GOP_BUFFER[i];
		Gop *node = get_first_elem(buffer);
		while (node) {
			int is_contain = false;
			int j;
			for (j = i + 1; j < OUTPUT_VIDEO_NUM; j++) {
				if (contained_in_queue(buffer, node)) {
					is_contain = true;
					break;
				}
			}
			if (!is_contain) {
				count++;
			}
      node = next_element(buffer, node);
		}
	}
	return count;
}

void print_gop_id_of_full_gop_buffer() {
	printf("Gop ids:");
	int i;
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		queue *buffer = FULL_GOP_BUFFER[i];
		Gop *node = get_first_elem(buffer);
		while (node) {
			printf(" %d", node->gop_id);
      node = next_element(buffer, node);
		}
		printf("\t");
	}
	printf("\n");
}

static void inline add_gop_to_full_gop_buffer(Gop *g, int full_gop_buffer_id) {
//	ListNode * node = new_ListNode(g);
//	sem_wait(&FULL_GOP_BUFFER_MUTEX[full_gop_buffer_id]);
//	insert_List(node, FULL_GOP_BUFFER[full_gop_buffer_id]);
//	sem_post(&FULL_GOP_BUFFER_MUTEX[full_gop_buffer_id]);
//	sem_post(&FULL_GOP_NUM[full_gop_buffer_id]);
  enqueue(FULL_GOP_BUFFER[full_gop_buffer_id], &g);
	return;
}


// EFFECT: add the given gop to FULL_GOP_BUFFER
void add_gop_to_full_gop_buffers(Gop *g) {
	int i;
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		add_gop_to_full_gop_buffer(g, ess[i].full_gop_buffer_id);
	}
//	sem_wait(&COUNT_FULL_GOP_BUFFER_REF_GOP);
#ifdef COUNT_GOP
//	printf("All full gop contain %d gops.\n", );
	print_gop_id_of_full_gop_buffer();
#endif

	return;
}

//// EFFECT: add the given gop to FULL_GOP_BUFFER
//void add_gop_to_full_gop_buffers(Gop *g) {
//	int i;
//	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
//		ListNode * node = new_ListNode(g);
//		sem_wait(&FULL_GOP_BUFFER_MUTEX[i]);
//		insert_List(node, FULL_GOP_BUFFER[i]);
//		sem_post(&FULL_GOP_BUFFER_MUTEX[i]);
//		sem_post(&FULL_GOP_NUM[i]);
//	}
//	//	sem_wait(&COUNT_FULL_GOP_BUFFER_REF_GOP);
//#ifdef COUNT_GOP
//	print_gop_id_of_full_gop_buffer();
//#endif
//
//	return;
//}

// EFFECT: remove the first gop from FULL_GOP_BUFFER
Gop *remove_gop_from_full_gop_buffer(int full_gop_buffer_id) {
  // count GOP_BUFFER
//	sem_wait(&FULL_GOP_NUM[full_gop_buffer_id]);
//	sem_wait(&FULL_GOP_BUFFER_MUTEX[full_gop_buffer_id]);
//	ListNode * node = remove_List(FULL_GOP_BUFFER[full_gop_buffer_id]);
//	sem_post(&FULL_GOP_BUFFER_MUTEX[full_gop_buffer_id]);
//	Gop *g = (Gop *)node->elem;
//	free(node);
  Gop **elem = (Gop **)dequeue(FULL_GOP_BUFFER[full_gop_buffer_id]);
	return *elem;
}

// GIVEN: the picture number of a decoded and scaled frame and the frame
// itself
// EFFECT: add the frame to the gop buffer
void add_scaled_frame_to_gop_buffer(int pict_num, uint8_t **buf) {
	int gop_id = pict_num / GOP_SIZE;
	int frame_id = pict_num % GOP_SIZE;

	sem_wait(&GOP_BUFFER_MUTEX);
	Gop *g = find_gop_in_gop_buffer(gop_id);
	if (g == NULL) {
		g = new_Gop(gop_id, GOP_SIZE);
		// FIXME: debug
//		fprintf(stdout, "evoke new_Gop from add_scaled_frame_to_gop_buffer\n");
		add_gop_to_gop_buffer(g);
	}
	sem_post(&GOP_BUFFER_MUTEX);

	sem_wait(&g->gop_mutex);
//	fprintf(stdout, "g->buf: %p, g->buf index: %p, buf: %p gop_buffer_memory start: %p end: %p\n", 
//			g->buf, &g->buf[SRC_PICTURE_SIZE * frame_id], buf, gop_buffer_memory.data, &gop_buffer_memory.data[gop_buffer_memory.blk_size * gop_buffer_memory.memory_size]);
	memcpy(&g->buf[SRC_PICTURE_SIZE * frame_id], buf[0], SRC_PICTURE_SIZE);
	// FIXME: debug
//	fprintf(stdout, "g->buf index: %p, buf: %p, finish memcpy debug\n", &g->buf[SRC_PICTURE_SIZE * frame_id], buf);
	g->frame_cnt += 1;
	g->valid[frame_id] = 1;
	if (g->frame_cnt == GOP_SIZE) {
		remove_gop_from_gop_buffer(g);

//		sem_wait(&COUNT_FULL_GOP_BUFFER_REF_GOP);
		add_gop_to_full_gop_buffers(g);
	}
	sem_post(&g->gop_mutex);

	return;
}

int has_period(){
	int has = 1;
	sem_wait(&PERIOD_LIST_MUTEX);
	if(is_empty_List(PERIOD_LIST)) has = 0;
	sem_post(&PERIOD_LIST_MUTEX);
	return has;
}

Period* get_period(){
	sem_wait(&PERIOD_LIST_MUTEX);
	if(is_empty_List(PERIOD_LIST)) return NULL;
	Period* p = (Period*)remove_List(PERIOD_LIST)->elem;
	sem_post(&PERIOD_LIST_MUTEX);
	return p;
}



// EFFECT: initializing decoding related data
int initialize_decode_data() {
	AVFormatContext * fmt_ctx = NULL;

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, SRC_FILENAME, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", SRC_FILENAME);
		exit(1);
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	int video_stream_idx = -1;
	AVStream *video_stream = NULL;
	AVCodecContext *video_dec_ctx = NULL;

	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;

		SRC_WIDTH = video_dec_ctx->width;
		SRC_HEIGHT = video_dec_ctx->height;
		SRC_PIX_FMT = video_dec_ctx->pix_fmt;
		CODEC_TIME_BASE = video_dec_ctx->time_base;
		STREAM_TIME_BASE = video_stream->time_base;
		TICKS_PER_FRAME = video_dec_ctx->ticks_per_frame;
		SAMPLE_ASPECT_RATIO = video_dec_ctx->sample_aspect_ratio;
	}

	uint8_t *video_dst_data[4] = {NULL};
	int      video_dst_linesize[4];
	int ret = av_image_alloc(video_dst_data, video_dst_linesize,
			video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt, 1);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate raw video buffer\n");
		exit(0);
	}
	av_free(video_dst_data[0]);

	SRC_PICTURE_SIZE = ret;

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, SRC_FILENAME, 0);

	if (!video_stream) {
		fprintf(stderr, "Could not find video stream in the input, aborting\n");
		exit(1);
	}

	GOP_BUFFER = new_List();
	sem_init(&GOP_BUFFER_MUTEX, 0, 1);

	FULL_GOP_BUFFER = (queue **)xmalloc(sizeof(queue *) * OUTPUT_VIDEO_NUM);
//	FULL_GOP_BUFFER_MUTEX = xmalloc(sizeof(sem_t) * OUTPUT_VIDEO_NUM);
//	FULL_GOP_NUM = xmalloc(sizeof(sem_t) * OUTPUT_VIDEO_NUM);

	sem_init(&VIDEO_FRAME_COUNT_MUTEX, 0 , 1);

	int i;
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
    FULL_GOP_BUFFER[i] = (queue *)malloc(sizeof(queue));
    initialize_queue(FULL_GOP_BUFFER[i], FULL_GOP_BUFFER_INITIAL_SIZE, sizeof(void *));
	}

//	core_binding_initialize();

	if(DEC_THREAD_CNT > 1){
		sem_init(&PERIOD_LIST_MUTEX, 0, 1);
		PERIOD_LIST = new_List();

		AVPacket pkt;
		AVFrame* frame = av_frame_alloc();
		int got_frame = 0;
		int finished = 0;
		long last_pts = 0;
		long pts = 0;
		Period* period = NULL;

		while(av_seek_frame(fmt_ctx, video_stream->index, pts, AVSEEK_FLAG_BACKWARD) >= 0){
			while(av_read_frame(fmt_ctx, &pkt) >= 0){
				if (pkt.stream_index == video_stream_idx) {
					if(avcodec_decode_video2(video_dec_ctx, frame, &got_frame, &pkt) >= 0){
						if(got_frame){
							pts = frame->pkt_pts;

							if(PTS_DURATION == -1){
								PTS_DURATION = frame->pkt_duration;
								START_PTS = frame->pkt_pts;

								period = xmalloc(sizeof(Period));
								period->start = pts;

								last_pts = pts;
								pts += DECODE_STEP * PTS_DURATION;
							}else{
								if(last_pts >= pts){
									finished = 1;
									period->end = LONG_MAX;
									insert_List(new_ListNode(period), PERIOD_LIST);
									break;
								}

								period->end = pts - 1;
								insert_List(new_ListNode(period), PERIOD_LIST);

								period = xmalloc(sizeof(Period));
								period->start = pts;

								last_pts = pts;
								pts += DECODE_STEP * PTS_DURATION;
							}

							// flush cached data
							pkt.data = NULL;
							pkt.size = 0;
							do{
								got_frame = 0;
								avcodec_decode_video2(video_dec_ctx, frame, &got_frame, &pkt);
							}while(got_frame);

							break;
						}
					}
				}
			}
			if(finished) break;
		}
	}

	avcodec_close(video_dec_ctx);
	avformat_close_input(&fmt_ctx);

	signal(SIGALRM, print_average_fps);  
	struct itimerval tick;  
	memset(&tick, 0, sizeof(tick));  

	tick.it_value.tv_sec = FPS_MEASURE_DURATION;  
	tick.it_value.tv_usec = 800 * 1000; 
	tick.it_interval.tv_sec = FPS_MEASURE_DURATION;  
	tick.it_interval.tv_usec = 0;  
	setitimer(ITIMER_REAL, &tick, NULL);  

	return 0;
}


int add_remaining_frame_to_full_gop_buffer() {
	while (length_List(GOP_BUFFER) > 0) {
		ListNode *node = remove_List(GOP_BUFFER);
		add_gop_to_full_gop_buffers((Gop *)node->elem);
		free(node);
	}
	return 0;
}

void decode_flush(AVPacket* p_pkt, int video_stream_idx, AVCodecContext* video_dec_ctx, AVFrame* frame, uint8_t** video_dst_data, int* video_dst_linesize, Period* p){
	int got_frame = 0;
	int frame_index = 0;

	p_pkt->data = NULL;
	p_pkt->size = 0;

	do {
		got_frame = 0;
		if (p_pkt->stream_index == video_stream_idx) {
			if(avcodec_decode_video2(video_dec_ctx, frame, &got_frame, p_pkt) >= 0){
				if (got_frame) {
					if(p == NULL || (frame->pkt_pts >= p->start && frame->pkt_pts <= p->end)){
						frame_index = av_rescale_q_rnd(frame->pkt_pts - START_PTS,  STREAM_TIME_BASE, CODEC_TIME_BASE, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX) / TICKS_PER_FRAME;
						av_image_copy(video_dst_data, video_dst_linesize,
								(const uint8_t **) (frame->data), frame->linesize,
								SRC_PIX_FMT, SRC_WIDTH, SRC_HEIGHT);

						sem_wait(&VIDEO_FRAME_COUNT_MUTEX);
						++VIDEO_FRAME_COUNT;
						++DECODED_FRAMES;
						sem_post(&VIDEO_FRAME_COUNT_MUTEX);

						// FIXME: debug
//						fprintf(stdout, "evoke add_scaled_frame_to_gop_buffer from decode_flush\n");
						add_scaled_frame_to_gop_buffer(frame_index, video_dst_data);
					}

				}
			}
		}
	} while (got_frame);

}

static inline EncodeStruct *slowest_encoding_task();
static inline int count_gop_of_encoding_task(EncodeStruct *es);
int scale_and_encode_gop(Gop *g, EncodeStruct *es);
inline bool all_encoding_tasks_finished();
int encode_and_scale_thread(void *arg);

// RETURNS: true iff the decoding thread should help with encoding task
bool should_decoding_thread_help_with_encoding_task() {
	if(count_gop_of_encoding_task(slowest_encoding_task()) > DEC_THREAD_TRANSFER_COND) {
		return true;
	} else {
		return false;
	}
}

// EFFECT: scheduling the decoding thread to help with the encoding task
void decoding_thread_help_with_encoding_task() {
	while(should_decoding_thread_help_with_encoding_task()) {
		EncodeStruct *es = slowest_encoding_task();
		int full_gop_buffer_id = es->full_gop_buffer_id;
		Gop *g = remove_gop_from_full_gop_buffer(full_gop_buffer_id);
		if(is_empty_Gop(g)) {
			return;
		} else {
			scale_and_encode_gop(g, es);
			free_Gop(g);
		}
	}
	return;
}



int decode_period() {
	TimeRecord tr = time_record_new("decode thread");
	time_record_interval_start(tr);
	binding_core();

	int i = 0;
	int ret, got_frame;
	AVFormatContext *fmt_ctx = NULL;

	AVCodecContext *video_dec_ctx = NULL;
	AVStream *video_stream = NULL;

	uint8_t *video_dst_data[4] = {NULL};
	int      video_dst_linesize[4];
	int video_dst_bufsize;

	int video_stream_idx = -1;
	AVFrame *frame = NULL;
	AVPacket pkt;

	sem_post(&ALIVED_DEC_THREAD_CNT);

	if (avformat_open_input(&fmt_ctx, SRC_FILENAME, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", SRC_FILENAME);
		exit(1);
	}

	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;
		ret = av_image_alloc(video_dst_data, video_dst_linesize,
				SRC_WIDTH, SRC_HEIGHT, SRC_PIX_FMT, 1);
		if (ret < 0) {
			fprintf(stderr, "Could not allocate raw video buffer\n");
			exit(1);
		}
		video_dst_bufsize = ret;
	} else {
		fprintf(stderr, "Error: no video data.\n");
		exit(1);
	}

	av_dump_format(fmt_ctx, 0, SRC_FILENAME, 0);

	frame = av_frame_alloc();

	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		exit(1);
	}

	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	Period* p = NULL;
	int period_finished = 0;
	int frame_index = 0;

#ifdef DEC_THREAD_SCHEDULING
	int checking_freq_of_dec_thread_in_frames = CHECKING_FREQ_OF_DEC_THREAD * GOP_SIZE;
#endif

	while(has_period()){
		p = get_period();
		period_finished = 0;
		if(av_seek_frame(fmt_ctx, video_stream->index, p->start, AVSEEK_FLAG_BACKWARD) < 0){
			fprintf(stderr, "Could not seek to pts %ld\n", p->start);
			exit(1);
		}
		while (av_read_frame(fmt_ctx, &pkt) >= 0) {
			do {
				got_frame = 0;
				if (pkt.stream_index == video_stream_idx) {
					ret = avcodec_decode_video2(video_dec_ctx, frame, &got_frame, &pkt);
					if (ret < 0) {
						fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
						return ret;
					}
					if (got_frame) {
						if(frame->pkt_pts >= p->start && frame->pkt_pts <= p->end){ 
							frame_index = av_rescale_q_rnd(frame->pkt_pts - START_PTS,  STREAM_TIME_BASE, CODEC_TIME_BASE, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX) / TICKS_PER_FRAME;
							av_image_copy(video_dst_data, video_dst_linesize,
									(const uint8_t **) (frame->data), frame->linesize,
									SRC_PIX_FMT, SRC_WIDTH, SRC_HEIGHT);

							sem_wait(&VIDEO_FRAME_COUNT_MUTEX);
							++VIDEO_FRAME_COUNT;
							++DECODED_FRAMES;
							sem_post(&VIDEO_FRAME_COUNT_MUTEX);

							// FIXME: debug
//							fprintf(stdout, "evoke add_scaled_frame_to_gop_buffer from decode_period\n");
							add_scaled_frame_to_gop_buffer(frame_index, video_dst_data);

#ifdef DEC_THREAD_SCHEDULING
							if (frame_index % checking_freq_of_dec_thread_in_frames == 0) {
								if(should_decoding_thread_help_with_encoding_task()) {
									decoding_thread_help_with_encoding_task();
								}
							}
#endif
						}
						if(frame->pkt_pts > p->end) period_finished = 1;
					}
				}

				pkt.data += ret;
				pkt.size -= ret;
			} while (pkt.size > 0);

			if(period_finished){
				decode_flush(&pkt, video_stream_idx, video_dec_ctx, frame, video_dst_data, video_dst_linesize, p);
				break;
			}
		}
	}

	decode_flush(&pkt, video_stream_idx, video_dec_ctx, frame, video_dst_data, video_dst_linesize, p);


	sem_wait(&ALIVED_DEC_THREAD_CNT);
	int alived_decode_thread_count;
	sem_getvalue(&ALIVED_DEC_THREAD_CNT, &alived_decode_thread_count); 

	// last decode thread
	if(alived_decode_thread_count == 0){
		add_remaining_frame_to_full_gop_buffer();
		for (i = 0; i < ENC_THREAD_CNT; i++) {
			Gop *empty_gop = new_Gop(-1, 0);
			add_gop_to_full_gop_buffers(empty_gop);
		}
		GOP_COUNT = (VIDEO_FRAME_COUNT + GOP_SIZE - 1) / GOP_SIZE;
	}

#ifdef DEC_THREAD_SCHEDULING
	while(!all_encoding_tasks_finished()) {
		EncodeStruct *es_slowest = slowest_encoding_task();
		encode_and_scale_thread(es_slowest);
	}
#endif

	avcodec_close(video_dec_ctx);
	avformat_close_input(&fmt_ctx);
	av_frame_free(&frame);
	av_free(video_dst_data[0]);

	time_record_interval_end(tr);
	time_record_interval_add(tr);
	time_record_print(tr);
	time_record_free(tr);

	return ret < 0;
}

int decode() {
	TimeRecord tr = time_record_new("decode thread");
	time_record_interval_start(tr);
	binding_core();

	int i;
	int ret, got_frame;
	AVFormatContext *fmt_ctx = NULL;

	AVCodecContext *video_dec_ctx = NULL;
	AVStream *video_stream = NULL;

	uint8_t *video_dst_data[4] = {NULL};
	int      video_dst_linesize[4];
	int video_dst_bufsize;

	int video_stream_idx = -1;
	AVFrame *frame = NULL;
	AVPacket pkt;

	if (avformat_open_input(&fmt_ctx, SRC_FILENAME, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", SRC_FILENAME);
		exit(1);
	}

	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;

		ret = av_image_alloc(video_dst_data, video_dst_linesize,
				SRC_WIDTH, SRC_HEIGHT, SRC_PIX_FMT, 1);
		if (ret < 0) {
			fprintf(stderr, "Could not allocate raw video buffer\n");
			exit(1);
		}
		video_dst_bufsize = ret;
	} else {
		fprintf(stderr, "Error: no video data.\n");
		exit(1);
	}

	av_dump_format(fmt_ctx, 0, SRC_FILENAME, 0);

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		exit(1);
	}

	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	int frame_index;

#ifdef DEC_THREAD_SCHEDULING
	int checking_freq_of_dec_thread_in_frames = CHECKING_FREQ_OF_DEC_THREAD * GOP_SIZE;
#endif

	while (av_read_frame(fmt_ctx, &pkt) >= 0) {
		do {
			got_frame = 0;
			if (pkt.stream_index == video_stream_idx) {
				ret = avcodec_decode_video2(video_dec_ctx, frame, &got_frame, &pkt);
				if (ret < 0) {
					fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
					return ret;
				}
				if (got_frame) {
					if(START_PTS == -1) START_PTS = frame->pkt_pts;
					frame_index = av_rescale_q_rnd(frame->pkt_pts - START_PTS,  STREAM_TIME_BASE, CODEC_TIME_BASE, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX) / TICKS_PER_FRAME;
					sem_wait(&VIDEO_FRAME_COUNT_MUTEX);
					++VIDEO_FRAME_COUNT;
					++DECODED_FRAMES;
					sem_post(&VIDEO_FRAME_COUNT_MUTEX);
					av_image_copy(video_dst_data, video_dst_linesize,
							(const uint8_t **) (frame->data), frame->linesize,
							SRC_PIX_FMT, SRC_WIDTH, SRC_HEIGHT);

					// FIXME: debug
//					fprintf(stdout, "evok add_scaled_frame_to_gop_buffer from decode\n");
					add_scaled_frame_to_gop_buffer(frame_index, video_dst_data);

#ifdef DEC_THREAD_SCHEDULING
					if (frame_index % checking_freq_of_dec_thread_in_frames
							== 0) {
						if (should_decoding_thread_help_with_encoding_task()) {
							decoding_thread_help_with_encoding_task();
						}
					}
#endif
				}
			}

			if (ret < 0)
				break;
			pkt.data += ret;
			pkt.size -= ret;
		} while (pkt.size > 0);
	}


	decode_flush(&pkt, video_stream_idx, video_dec_ctx, frame, video_dst_data, video_dst_linesize, NULL);

	add_remaining_frame_to_full_gop_buffer();
	for (i = 0; i < ENC_THREAD_CNT; i++) {
		Gop *empty_gop = new_Gop(-1, 0);
		add_gop_to_full_gop_buffers(empty_gop);
	}
	GOP_COUNT = (VIDEO_FRAME_COUNT + GOP_SIZE - 1) / GOP_SIZE;

#ifdef DEC_THREAD_SCHEDULING
	while(!all_encoding_tasks_finished()) {
		EncodeStruct *es_slowest = slowest_encoding_task();
		encode_and_scale_thread(es_slowest);
	}
#endif

	avcodec_close(video_dec_ctx);
	avformat_close_input(&fmt_ctx);
	av_frame_free(&frame);
	av_free(video_dst_data[0]);

	time_record_interval_end(tr);
	time_record_interval_add(tr);
	time_record_print(tr);
	time_record_free(tr);

	return ret < 0;
}


/*******************Encode related functions**********************************/
List * PACKET_BUFFER;
sem_t PACKET_BUFFER_MUTEX;

typedef struct GopPackets {
	AVPacket **packets;
	int count;
	int gop_id;
}GopPackets;

GopPackets *new_GopPackets(int count, int gop_id) {
	GopPackets *pa = xmalloc(sizeof(GopPackets));
	pa->packets = xcalloc(count, sizeof(AVPacket));
	pa->count = count;
	pa->gop_id = gop_id;
	return pa;
}

bool is_empty_GopPackets(GopPackets *gp) {
	REQUIRES(gp != NULL);
	return gp->count == 0;
}

// RETURNS: negative value iff the gop_id of gop in n1 is less
// than the gop_id of gop in n2, 0 if equal, positive if greater
int cmp_GopPackets(ListNode *n1, ListNode *n2) {
	return ((GopPackets *)n1->elem)->gop_id - ((GopPackets *)n2->elem)->gop_id;
}

// EFFECT: add the packets of a gop to PACKET_BUFFER
void add_packets_to_packet_buffer(GopPackets *packets) {
	ListNode *node = new_ListNode(packets);
	sem_wait(&PACKET_BUFFER_MUTEX);
	insert_by_order_List(node, PACKET_BUFFER, cmp_GopPackets);
	sem_post(&PACKET_BUFFER_MUTEX);
	return;
}

// EFFECT: remove the packets from a gop to PACKET_BUFFER
// RETURNS: the removed packets
GopPackets *remove_packets_from_packet_buffer() {
	sem_wait(&PACKET_BUFFER_MUTEX);
	ListNode *node = remove_List(PACKET_BUFFER);
	sem_post(&PACKET_BUFFER_MUTEX);
	GopPackets *packets = (GopPackets *)node->elem;
	free(node);
	return packets;

}

int running_encode_thread = 0;
sem_t encode_thread_mutex;


// EFFECT: initializing encoding related data
int initialize_encode_data() {
	PACKET_BUFFER = new_List();
	sem_init(&PACKET_BUFFER_MUTEX, 0, 1);
	sem_init(&encode_thread_mutex, 0, 1);
	sem_init(&HELP_WITH_OTHER_TASK, 0, 1);


	int i;
	for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		ess[i].running_thread = 0;
		sem_init(&ess[i].running_thread_mutex, 0, 1);
	}

	return 0;
}

void get_filename(char *dir, int id, char*buf) {
}

int scale_and_encode_gop(Gop *g, EncodeStruct *es) {
	int dst_w = es->dst_width, dst_h = es->dst_height;
	enum AVPixelFormat dst_pix_fmt = es->dst_pix_fmt;
	int bit_rate = es->bitrate;

	char video_dst_filename[100];
	sprintf(video_dst_filename, "./%s/%d.h264", es->output_dir, g->gop_id);

	AVCodecContext* pCodecCtx;
	AVStream* video_st;
	AVFormatContext* pFormatCtx;

	sem_wait(&encode_thread_mutex);
	running_encode_thread++;
	sem_post(&encode_thread_mutex);

	AVOutputFormat* fmt;
	AVCodec* pCodec;
	AVPacket pkt;

	pFormatCtx = avformat_alloc_context();
	fmt = av_guess_format(NULL, video_dst_filename, NULL);
	pFormatCtx->oformat = fmt;

	if (avio_open(&pFormatCtx->pb,video_dst_filename, AVIO_FLAG_READ_WRITE) < 0){
		printf("Failed to open output file! \n");
		return -1;
	}

	video_st = avformat_new_stream(pFormatCtx, 0);
	if (video_st==NULL){
		return -1;
	}

	video_st->time_base = STREAM_TIME_BASE;

	pCodecCtx = video_st->codec;
	pCodecCtx->codec_id = fmt->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = dst_pix_fmt;
	pCodecCtx->width = dst_w;
	pCodecCtx->height = dst_h;
	pCodecCtx->time_base = CODEC_TIME_BASE;

	pCodecCtx->bit_rate = bit_rate;
//	pCodecCtx->rc_max_rate = bit_rate;
//	pCodecCtx->rc_min_rate = bit_rate;
//	pCodecCtx->rc_buffer_size = bit_rate;
//	pCodecCtx->bit_rate_tolerance = 0;

	pCodecCtx->ticks_per_frame = TICKS_PER_FRAME;
//	pCodecCtx->gop_size= GOP_SIZE;
	pCodecCtx->gop_size= 5;
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	pCodecCtx->max_b_frames = 2;

	// Set Option
	AVDictionary *param = 0;

	//H.264
	if(pCodecCtx->codec_id == AV_CODEC_ID_H264) {
//		av_dict_set(&param, "preset", "slow", 0);
		av_dict_set(&param, "preset", "medium", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);
	}

	//Show some Information
	av_dump_format(pFormatCtx, 0, video_dst_filename, 1);

	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (!pCodec){
		printf(" %s Can not find encoder! \n", "scale and encode ");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
		printf("%s Failed to open encoder! \n", "scale and encode ");
		return -1;
	}

	uint8_t *src_data[4], *dst_data[4], *tmp_data[4];
	int src_linesize[4], dst_linesize[4], tmp_linesize[4];
//	int tmp_size = 0;
	int ret;

	if ((ret = av_image_alloc(src_data, src_linesize, SRC_WIDTH, SRC_HEIGHT,
					SRC_PIX_FMT, 16)) < 0) {
		fprintf(stderr, "Could not allocate source image\n");
		exit(1);
	}

	if ((ret = av_image_alloc(dst_data, dst_linesize, dst_w, dst_h, dst_pix_fmt,
					32)) < 0) {
		fprintf(stderr, "Could not allocate destination image\n");
		exit(1);
	}

	av_freep(&src_data[0]);
	av_freep(&dst_data[0]);

	/* buffer is going to be written to rawvideo file, no alignment */
//	if ((ret = av_image_alloc(tmp_data, tmp_linesize, SRC_WIDTH, SRC_HEIGHT, dst_pix_fmt,
//					1)) < 0) {
//		fprintf(stderr, "Could not allocate destination image\n");
//		exit(1);
//	}
//	tmp_size = ret;

	/* create scaling context */
	struct SwsContext *sws_ctx = sws_getContext(SRC_WIDTH, SRC_HEIGHT, SRC_PIX_FMT, dst_w, dst_h,
			dst_pix_fmt,
			SWS_BILINEAR, NULL, NULL, NULL);
	if (!sws_ctx) {
		fprintf(stderr, "Impossible to create scale context for the conversion "
				"fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
				av_get_pix_fmt_name(SRC_PIX_FMT), SRC_WIDTH, SRC_HEIGHT,
				av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h);
		AVERROR(EINVAL);
		exit(1);
	}

	avformat_write_header(pFormatCtx,NULL);

	int picture_size = avpicture_get_size(dst_pix_fmt, dst_w, dst_h);
	AVFrame* dst_frame = av_frame_alloc();
	dst_frame->width = dst_w;
	dst_frame->height = dst_h;
	dst_frame->format = dst_pix_fmt;
	uint8_t *picture_buf = (uint8_t *) av_malloc(picture_size);
	avpicture_fill((AVPicture *)dst_frame, picture_buf, dst_pix_fmt, dst_w, dst_h);

	int src_picture_size = avpicture_get_size(SRC_PIX_FMT, SRC_WIDTH, SRC_HEIGHT);
	AVFrame *src_frame = av_frame_alloc();
	src_frame->width = SRC_WIDTH;
	src_frame->height = SRC_HEIGHT;
	src_frame->format = SRC_PIX_FMT;

	av_new_packet(&pkt,picture_size);

	int got_picture = 0;
	int i;
	for (i = 0; i < GOP_SIZE; i++) {
		if(g->valid[i] == 0) continue;

		got_picture = 0;
		uint8_t *buf = &g->buf[src_picture_size * i];
		avpicture_fill((AVPicture *)src_frame, buf, SRC_PIX_FMT, SRC_WIDTH, SRC_HEIGHT);

		if(dst_w == SRC_WIDTH && dst_h == SRC_HEIGHT){
			dst_frame = src_frame;
		}else{
			sws_scale(sws_ctx, (const uint8_t **)src_frame->data,
					src_frame->linesize, 0, SRC_HEIGHT, dst_frame->data, dst_frame->linesize);
		}

		dst_frame->pts = START_PTS + (g->gop_id * GOP_SIZE + i) * PTS_DURATION;

		int ret = avcodec_encode_video2(pCodecCtx, &pkt, dst_frame,
				&got_picture);

		if (ret < 0) {
			printf("%s Failed to encode! \n", "scale and encode ");
			return -1;
		}

		sem_wait(&es->running_thread_mutex);
		es->encoded_frame_cnt += 1;
		sem_post(&es->running_thread_mutex);

		if (got_picture) {
			pkt.stream_index = video_st->index;
			ret = av_write_frame(pFormatCtx, &pkt);
			av_free_packet(&pkt);
		}
	}

	ret = flush_encoder(pFormatCtx,0);
	if (ret < 0) {
		printf("Flushing encoder failed\n");
		return -1;
	}

	av_write_trailer(pFormatCtx);

	if (video_st){
		avcodec_close(video_st->codec);
		av_free(dst_frame);
	}

	avio_close(pFormatCtx->pb);
	avformat_free_context(pFormatCtx);
	sws_freeContext(sws_ctx);

	return 0;
}

// EFFECT: output the list of video segments into a single file
void produce_one_video_segment_list_file(EncodeStruct *es) {

	char filename[100];
	sprintf(filename, "./%s/mylist.txt", es->output_dir);
	FILE *f = fopen(filename, "w");
	int j;
	for (j = 0; j < GOP_COUNT; j++) {
		fprintf(f, "file '%d.h264'\n", j);
	}
	fclose(f);
	return;
}

// EFFECT: combine all segments of one output into a single file
void output_one_video(EncodeStruct *es) {

	char command[200];
	sprintf(command, "output_one_video%d",es->full_gop_buffer_id);

	TimeRecord tr = time_record_new((char *) command);
	time_record_interval_start(tr);

	produce_one_video_segment_list_file(es);

	int i = es->full_gop_buffer_id;
	sprintf(command, "ffmpeg -f concat -i ./%s/mylist.txt -c copy -y %s",  ess[i].output_dir, ess[i].output_filename);
	system(command);

	time_record_interval_end(tr);
	time_record_interval_add(tr);
	time_record_print(tr);
	time_record_free(tr);
}



// RETURNS: the number of gop in FULL_GOP_BUFFER of es
static inline int count_gop_of_encoding_task(EncodeStruct *es) {
	queue *gop_queue = FULL_GOP_BUFFER[es->full_gop_buffer_id];
	return length_of_queue(gop_queue);
}

// RETURNS: the EncodeStruct of the slowest encoding task
static inline EncodeStruct *slowest_encoding_task() {
	int i, max, max_len;
	max = 0;
	max_len = count_gop_of_encoding_task(&ess[0]);
	for(i = 1; i < OUTPUT_VIDEO_NUM; i++) {
		int len = count_gop_of_encoding_task(&ess[i]);
		if(len > max_len) {
			max = i;
			max_len = len;
		}
	}
	return &ess[max];
}

// RETURNS: true iff the encoding task es is FASTER_IN_GOP GOPs faster than
// the slowest encoding task
static inline bool is_fast_enough(EncodeStruct *es) {
	EncodeStruct *slowest_es = slowest_encoding_task();
	bool ret = (count_gop_of_encoding_task(slowest_es) - count_gop_of_encoding_task(es)) > FASTER_IN_GOP;
	return ret;
}


// RETURNS: true iff the encoding task of es has the least number of
// unencoded gop
static inline bool has_least_unencoded_gop(EncodeStruct *es) {
	int i, n;
	n = count_gop_of_encoding_task(es);
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		if(count_gop_of_encoding_task(&ess[i]) < n) {
			return false;
		}
	}
	return true;
}

// RETURNS: the first Gop of encoding task es(but doesn't remove it from FULL_GOP_BUFFERS)
inline Gop *peek_first_gop(EncodeStruct *es) {
  return (Gop *)get_first_elem(FULL_GOP_BUFFER[es->full_gop_buffer_id]);
}

// RETURNS: true iff the encoding task es was finished
inline bool is_encoding_task_finished(EncodeStruct *es) {
	bool ret = (count_gop_of_encoding_task(es) == 0);
	ret = ret || (is_empty_Gop(peek_first_gop(es)));
	return ret;
}

// RETURNS: true iff all encoding tasks were finished
inline bool all_encoding_tasks_finished() {
	int i;
	for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		if(!is_encoding_task_finished(&ess[i])) {
			return false;
		}
	}
	return true;
}

// GIVEN: the number of encoded GOPs of an encoding and its EncodeStruct
// RETURNS: true iff the thread should help with other encoding task
static inline bool should_help_with_other_encoding_tasks(int encoded_gop, EncodeStruct *es) {
	return (is_encoding_task_finished(es) && !all_encoding_tasks_finished())
			|| ((encoded_gop % CHECKING_FREQ == 0) && (OUTPUT_VIDEO_NUM > 1)
					&& has_least_unencoded_gop(es) && is_fast_enough(es));
}

void transfer_to_task(EncodeStruct **src_es_p, int *full_gop_buffer_id_p, EncodeStruct *dst_es){
	*src_es_p = dst_es;
	*full_gop_buffer_id_p = dst_es->full_gop_buffer_id;
}

int encode_and_scale_thread(void *arg) {

	printf("Entering encode thread.\n");

	TimeRecord tr = time_record_new((char *)" encode and scale thread");
	time_record_interval_start(tr);

	EncodeStruct *original_es = (EncodeStruct *)arg;
	EncodeStruct * es = original_es;
	int full_gop_buffer_id = es->full_gop_buffer_id;

#ifdef ENC_THREAD_TRANSFER_RECORDING
	pthread_t pid = pthread_self();
	printf("thread %u encoding task %d\n", pid, es->full_gop_buffer_id);
#endif

	sem_wait(&original_es->running_thread_mutex);
	original_es->running_thread++;
	sem_post(&original_es->running_thread_mutex);

	binding_core();

//	PRINT_DYNAMIC_FPS = 1;

//	int gop_size = GOP_SIZE;
	int encoded_gop_count;

loop:
	encoded_gop_count = 0;  // encoded gop of encoding task es
	Gop *g = remove_gop_from_full_gop_buffer(full_gop_buffer_id);

	while (!is_empty_Gop(g)) {

        if(BUSY_ENCODE_THREAD_COUNT < ENC_THREAD_CNT) ++BUSY_ENCODE_THREAD_COUNT;
        else if(BUSY_ENCODE_THREAD_COUNT == ENC_THREAD_CNT) {
        	PRINT_DYNAMIC_FPS = 1;
        	++BUSY_ENCODE_THREAD_COUNT;
        }

		scale_and_encode_gop(g, es);
		encoded_gop_count++;
		free_Gop(g);

#ifdef ENC_THREAD_SCHEDULING
		if(should_help_with_other_encoding_tasks(encoded_gop_count, es)) {

			if(sem_trywait(&HELP_WITH_OTHER_TASK) == 0) { //success
				EncodeStruct *es_slowest = slowest_encoding_task();

				g = remove_gop_from_full_gop_buffer(
						es_slowest->full_gop_buffer_id);
				if(is_empty_Gop(g)) {
					add_gop_to_full_gop_buffer(g, es_slowest->full_gop_buffer_id);
				} else {
					scale_and_encode_gop(g, es_slowest);
					free_Gop(g);
					transfer_to_task(&es, &full_gop_buffer_id, es_slowest);
					encoded_gop_count = 1;
				}
				sem_post(&HELP_WITH_OTHER_TASK);
			}
		}
#endif

		g = remove_gop_from_full_gop_buffer(full_gop_buffer_id);
	}
	add_gop_to_full_gop_buffer(g, full_gop_buffer_id);

	PRINT_DYNAMIC_FPS = 0;

#ifdef ENC_THREAD_SCHEDULING
	if(!all_encoding_tasks_finished()) {
		EncodeStruct *es_slowest = slowest_encoding_task();
		transfer_to_task(&es, &full_gop_buffer_id, es_slowest);
		goto loop;
	}
#endif

	int v;
	sem_wait(&original_es->running_thread_mutex);
	original_es->running_thread--;
	v = original_es->running_thread;
	sem_post(&original_es->running_thread_mutex);

	if(v == 0) {  // this is the last thread
		output_one_video(original_es);
	}


    time_record_interval_end(tr);
    time_record_interval_add(tr);
    time_record_print(tr);
    time_record_free(tr);

	return 0;
}


// without dynamic thread scheduling between different encoding task
int encode_and_scale_thread0 (void *arg) {
	EncodeStruct *es = (EncodeStruct *)arg;
	int full_gop_buffer_id = es->full_gop_buffer_id;

	sem_wait(&es->running_thread_mutex);
	es->running_thread++;
	sem_post(&es->running_thread_mutex);

	TimeRecord tr = time_record_new((char *)" encode and scale thread");
	time_record_interval_start(tr);


	sem_wait(&encode_thread_mutex);
	running_encode_thread++;
	sem_post(&encode_thread_mutex);

//	PRINT_DYNAMIC_FPS = 1;

	binding_core();

	Gop *g = remove_gop_from_full_gop_buffer(full_gop_buffer_id);

	while (!is_empty_Gop(g)) {
		scale_and_encode_gop(g, es);
		free_Gop(g);
		g = remove_gop_from_full_gop_buffer(full_gop_buffer_id);
	}

	time_record_interval_end(tr);
	time_record_interval_add(tr);
	time_record_print(tr);
	time_record_free(tr);

	sem_wait(&es->running_thread_mutex);
	es->running_thread--;
	sem_post(&es->running_thread_mutex);

	if(es->running_thread == 0) {  // this is the last thread
		PRINT_DYNAMIC_FPS = 0;
		output_one_video(es);
	}

	return 0;
}

#ifdef AUTOMATIC_ALLOC_THREAD

void allocate_threads() {
//	static int encode_threads[6] = {4, 3, 2, 2, 1, 1};
	static double encode_threads[6] = {4, 3, 2, 2, 0.5, 0.5};
	int encoding_tasks[6];

	if(THREAD_CNT < 2) {
		fprintf(stderr, "Error: thread number less than 2.\n");
		exit(1);
	}

//	int encode_threads_per_decode_thread = 0;
	double encode_threads_per_decode_thread = 0;
	int i;
	for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		if(ess[i].dst_width == 1920 && ess[i].dst_height == 1080
				&& ess[i].bitrate == 8000000) {
			encode_threads_per_decode_thread += encode_threads[0];
			encoding_tasks[i] = 0;
		} else if(ess[i].dst_width == 1920 && ess[i].dst_height == 1080
				&& ess[i].bitrate == 4000000) {
			encode_threads_per_decode_thread += encode_threads[1];
			encoding_tasks[i] = 1;
		} else if(ess[i].dst_width == 1280 && ess[i].dst_height == 720
				&& ess[i].bitrate == 2300000) {
			encode_threads_per_decode_thread += encode_threads[2];
			encoding_tasks[i] = 2;
		} else if(ess[i].dst_width == 1280 && ess[i].dst_height == 720
				/*&& ess[i].bitrate == 1300000 */) {
			encode_threads_per_decode_thread += encode_threads[3];
			encoding_tasks[i] = 3;
		} else if(ess[i].dst_width == 640 && ess[i].dst_height == 360
				&& ess[i].bitrate == 800000) {
			encode_threads_per_decode_thread += encode_threads[4];
			encoding_tasks[i] = 4;
		} else if(ess[i].dst_width == 640 && ess[i].dst_height == 360
				&& ess[i].bitrate == 600000) {
			encode_threads_per_decode_thread += encode_threads[5];
			encoding_tasks[i] = 5;
		} else {
			printf("invalid resolution %dx%d or bitrate %d.\n",
					ess[i].dst_width, ess[i].dst_height, ess[i].bitrate);
			exit(1);
		}
	}

	int decode_threads = THREAD_CNT / (encode_threads_per_decode_thread + 1);

	int allocated_encode_threads = 0;

	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		ess[i].thread_num = (int)((double)decode_threads * encode_threads[encoding_tasks[i]] + 0.5);
		allocated_encode_threads += ess[i].thread_num;
	}

//	int threads_remain = THREAD_CNT % (encode_threads_per_decode_thread + 1);
	int threads_remain = THREAD_CNT - allocated_encode_threads - decode_threads;
	if(threads_remain > 0) {
		decode_threads++;
		threads_remain--;
		for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
			ess[i].thread_num += threads_remain / OUTPUT_VIDEO_NUM;
		}
		ess[0].thread_num += threads_remain % OUTPUT_VIDEO_NUM;
	}
	DEC_THREAD_CNT = decode_threads;

	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		printf("Encoding task %d has %d threads.\n", encoding_tasks[i], ess[i].thread_num);
	}
	printf("Decoding task has %d threads.\n", DEC_THREAD_CNT);

	return;
}
#endif


int main (int argc, char **argv)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	char command[256];
	int devNull = open("/dev/null", O_WRONLY);
	dup2(devNull, STDERR_FILENO);

	OUTPUT_VIDEO_NUM = 0;

	printf("\n");
	for(i = 1; i < argc; ++i){
		char* arg = argv[i];
		if(strcmp("-i", arg) == 0){
			SRC_FILENAME = argv[++i];
			printf("Input File Name: %s\n", argv[i]);
		}else if(strcmp("-c:v", arg) == 0){
			printf("%s\n", argv[++i]);
		}else if(strcmp("-b:v", arg) == 0){
			sscanf(argv[++i], "%dk", &ess[OUTPUT_VIDEO_NUM].bitrate);
			ess[OUTPUT_VIDEO_NUM].bitrate *= 1000;
			printf("Bitrate :%s\n", argv[i]);
		}
		else if(strcmp("-s", arg) == 0){
			sscanf(argv[++i], "%dx%d", &ess[OUTPUT_VIDEO_NUM].dst_width, &ess[OUTPUT_VIDEO_NUM].dst_height);
			printf("Resolution :%s\n", argv[i]);
		}
		else if(strcmp("-c:a", arg) == 0){
			printf("%s\n", argv[++i]);
			ess[OUTPUT_VIDEO_NUM].output_filename = argv[++i];
			struct timeval t;
			gettimeofday(&t, NULL);
			long timestamp = t.tv_sec * 1000000 + t.tv_usec;
			sprintf(ess[OUTPUT_VIDEO_NUM].output_dir, "%s_%ld", ess[OUTPUT_VIDEO_NUM].output_filename, timestamp); 
			printf("Output File name: %s\n", argv[i]);
			printf("Output Dir name: %s\n", ess[OUTPUT_VIDEO_NUM].output_dir);
			HAS_AUDIO = 1;
			++OUTPUT_VIDEO_NUM;
		}
#ifndef AUTOMATIC_ALLOC_THREAD
		else if(strcmp("-dtn", arg) == 0){
			DEC_THREAD_CNT = atoi(argv[++i]);
			printf("Decode Thread Count: %s\n", argv[i]);
		}
#endif

		else if(strcmp("-fc", arg) == 0){
			ID_OF_THE_FIRST_CORE_TO_BIND = atoi(argv[++i]);
			printf("First Bingding Core: %s\n", argv[i]);
		}
#ifndef AUTOMATIC_ALLOC_THREAD
		else if(strcmp("-etn", arg) == 0){
			ess[OUTPUT_VIDEO_NUM].thread_num = atoi(argv[++i]);
			printf("Encode Thread Count: %s\n", argv[i]);
		}
#endif

#ifdef AUTOMATIC_ALLOC_THREAD
		else if (strcmp("-threads", arg) == 0) {
			THREAD_CNT = atoi(argv[++i]);
			printf("Thread Count: %s\n", argv[i]);
		}
#endif
		else if(strcmp("-quiet", arg) == 0){
			++i;
			CONSOL_OUTPUT = 0;
		}
	}

#ifdef AUTOMATIC_ALLOC_THREAD
	allocate_threads();
#endif

	if(CONSOL_OUTPUT == 0){
		dup2(devNull, STDOUT_FILENO);
	}

	for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		sprintf(command, "rm -rf %s && mkdir %s", ess[i].output_dir, ess[i].output_dir);
		system(command);
	}

	if(OUTPUT_VIDEO_NUM < 1 || OUTPUT_VIDEO_NUM > 6) {
		fprintf(stderr, "output video number should be in range [1, 6].\n");
		exit(1);
	}

	sem_init(&ALIVED_DEC_THREAD_CNT, 0, 0);

	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		ENC_THREAD_CNT += ess[i].thread_num;
	}

	THREAD_COUNT = ENC_THREAD_CNT + DEC_THREAD_CNT;

	av_register_all();

	initialize_decode_data(); 

	initialize_encode_data(); 
	core_binding_initialize();

#ifdef PATCH_MEMORY_H
	initialize_ss_memory(&gop_structure_memory, sizeof(Gop), 50, "gop_structure");
	initialize_ss_memory(&gop_buffer_memory, SRC_PICTURE_SIZE * GOP_SIZE, 50, "gop_buffer");
#endif

#ifdef RECORD_TIME_COST
	initialize_time_records();
#endif

	pthread_t* decode_threads = xmalloc(DEC_THREAD_CNT * sizeof(pthread_t));

	if(DEC_THREAD_CNT > 1){
		for (i = 0; i < DEC_THREAD_CNT; i++) {
			pthread_create(&decode_threads[i], NULL, (void * (*)(void *))decode_period, NULL);
		}

		for (i = 0; i < DEC_THREAD_CNT; i++) {
			pthread_detach(decode_threads[i]);
		}
	}else{
			pthread_create(&decode_threads[0], NULL, (void * (*)(void *))decode, NULL);
			pthread_detach(decode_threads[0]);
	}

	pthread_t* encode_threads = xmalloc(ENC_THREAD_CNT * sizeof(pthread_t));
	int thread_id = 0;
	for (i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		for (j = 0; j < ess[i].thread_num; j++) {
			pthread_create(&encode_threads[thread_id++], NULL, (void * (*)(void *))encode_and_scale_thread, (void *)&ess[i]);
		}
	}

	for (i = 0; i < thread_id; i++) {
		pthread_join(encode_threads[i], NULL);
	}

#ifdef RECORD_TIME_COST
	print_time_records();
	free_time_records();
#endif

	/*if(HAS_AUDIO){*/
	/*sprintf(command, "ffmpeg -i %s -vn -acodec copy tmp.aac -y", SRC_FILENAME);*/

	/*for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {*/
	/*sprintf(command, "ffmpeg -y -i %s -i tmp.aac -c:v copy -c:a copy %s -y", ess[i].output_filename, ess[i].output_filename);*/
	/*system(command);*/
	/*}*/

	/*system("rm -rf tmp.aac");*/
	/*}*/

	for(i = 0; i < OUTPUT_VIDEO_NUM; i++) {
		sprintf(command, "rm -rf %s", ess[i].output_dir);
		system(command);
	}

	// TODO: change
//	printf("Average FPS is: %.2f \n", AVERAGE_FPS / FPS_COUNT);
	FILE *fps_output;
	fps_output = fopen("fps_output", "a");
	fprintf(fps_output, "ss_memory: Average FPS is: %.2f \n", AVERAGE_FPS / FPS_COUNT);
	fclose(fps_output);

#ifdef PATCH_MEMORY_H
	finalize_ss_memory(&gop_structure_memory);
	finalize_ss_memory(&gop_buffer_memory);
#endif

	return ret < 0;
}
