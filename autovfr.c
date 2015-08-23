#include <stdio.h>
#include <json/json.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef _WIN32
#include <limits.h>
const char *pathseparator = "/";
#else
const char *pathseparator = "\\";
#define PATH_MAX 260
#define realpath(A, B) _fullpath((B), (A), PATH_MAX)
#endif

char *logfile = '\0';
char *jsonfile = '\0';

typedef struct {
	int index;
	int pair_a;
	int pair_b;
	int result_cadence;
} pair_data;

typedef struct {
	unsigned long total_frames;
	unsigned long total_dwframes;
	unsigned long dw_size;
	int *comb_list;
	int *crop_comb_list;
	int *move_comb_list;
	unsigned long dw_blocks;
	pair_data *pair;
} frame_data;

const int blank_cadence[] = {0,0,0,0,0,0,0,0,0,0};
const int telop_cadence[] = {1,1,1,1,1,1,1,1,1,1};
const int deint01_cadence[] = {0,1,0,1,0,1,0,1,0,1};
const int deint02_cadence[] = {1,0,1,0,1,0,1,0,1,0};
const int deint_half_cadence[] = {0,1,0,1,0,1};

int bob_flag = 0;
int last_index_number = 0;

int pair_count[] = {0, 0, 0, 0, 0};
int p_count = 0;

typedef struct {
	/* comb list after Detected cadence */
	int comb_frame;
	int comb_current_cadence;
} comb_data;

typedef struct {
	/* Cadence List */
	int cadence_count;
	int *cadence_index;
	int *cadence_num;
	/* Sima and Bob List */
	int comb_count;
	comb_data *comb;
} cadence_data;

int seq_threshold = 3;
int skip_pair = 3;
int enable_bob = 0;
int detect_comb_num = 3;

int verbose = 0;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define DEBUG_INFO 0
#define DEBUG_DEBUG 1
#define DEBUG_HUGE 2
#define DEBUG_SHOW_COMB 4

#define TELECIDE_TYPE_REF0 0
#define TELECIDE_TYPE_REF1 1
#define TELECIDE_TYPE_REF2 2
#define TELECIDE_TYPE_REF3 3
#define TELECIDE_TYPE_REF4 4
#define TELECIDE_TYPE_NONE 5
#define TELECIDE_TYPE_BOB  6
#define TELECIDE_TYPE_DINT 7

void debug_print(int level, const char * format, ...) {
	va_list b;
	va_start(b, format);
	if (verbose >= level) {
		vprintf(format, b); 
	}
	va_end(b);
}

void get_max_pair(const int *p, int *key, int *val) {
	int i;
	int max = -1;
	for (i = 0; i < 5; i++) {
		if (max < p[i]) {
			max = p[i];
			*val = p[i];
			*key = i;
		}
	}
}

void test_parse_obj_to_string(struct json_object* const obj) {
	json_object_object_foreach(obj, key, val) {
		printf("-- \t%s: %s\n", key, json_object_to_json_string(val));
	}
}

void free_data(frame_data *d) {
	free(d->pair);
	free(d->comb_list);
	free(d->crop_comb_list);
	free(d->move_comb_list);
	free(d);
}

//
//ログファイル読み込み
//
int read_file(const char *filename, frame_data *d) { //{{{
	FILE *fp;
	char buf[512];
	char first_line[512];
	unsigned int frame = 0;
	fp = fopen(filename, "rt");
	if (!fp) {
		fprintf(stderr, "入力ファイルオープンエラー\n");
		return 0;
	}
	fgets(first_line, sizeof(first_line), fp);
	sscanf(first_line, "total_f-%ld", &(d->total_frames));
	fgets(first_line, sizeof(first_line), fp);
	sscanf(first_line, "doubleweave_f-%ld", &(d->total_dwframes));
	unsigned long dw_size = d->total_dwframes + (10 - (d->total_dwframes % 10)) % 10;
	d->dw_size = dw_size;
	d->comb_list = malloc(sizeof(int) * dw_size);
	d->crop_comb_list = malloc(sizeof(int) * dw_size);
	d->move_comb_list = malloc(sizeof(int) * dw_size);
	memset(d->comb_list, 0, sizeof(int) * dw_size);
	memset(d->crop_comb_list, 0, sizeof(int) * dw_size);
	memset(d->move_comb_list, 0, sizeof(int) * dw_size);
	int comb, ccomb, mcomb;
	while (fgets(buf, sizeof(buf), fp)) {
		sscanf(buf, "%d-%d-%d-%d", &frame, &comb, &ccomb, &mcomb);
		memcpy(d->comb_list + frame, &comb, sizeof(int));
		memcpy(d->crop_comb_list + frame, &ccomb, sizeof(int));
		memcpy(d->move_comb_list + frame, &mcomb, sizeof(int));
	}
	fclose(fp);
	return 1;
}
//}}}

void get_ref_string(const int ref_type, char **ref_str) {
	switch (ref_type) {
		case 0:
			*ref_str = "pref_0";
			break;
		case 1:
			*ref_str = "pref_1";
			break;
		case 2:
			*ref_str = "pref_2";
			break;
		case 3:
			*ref_str = "pref_3";
			break;
		case 4:
			*ref_str = "pref_4";
			break;
		case 6:
			*ref_str = "bob";
			break;
		default:
			*ref_str = "-";
			break;
	}
}

//
//jsonオブジェクトを生成します
//
int create_json(const frame_data *f, const cadence_data *c, json_object *output) { //{{{
	int i;
	json_object *cadence_list = json_object_new_array();
	json_object *comb_list = json_object_new_array();
	int start_frame = 0;
	int end_frame = 0;
	int bob_ivtc60mc_ref = -1;
	int cur_cadence = -1;
	char *cadence_str[16];
	char *cur_cadence_str[16];
	//create cadence data
	for (i = 0; i < c->cadence_count; i++) {
		cur_cadence = c->cadence_num[i];
		if (i == c->cadence_count - 1) {
			end_frame = f->dw_size;
			if (!enable_bob && cur_cadence == TELECIDE_TYPE_BOB) cur_cadence = c->cadence_num[i - 1];
		} else {
			end_frame = c->cadence_index[i + 1];
			if (!enable_bob && cur_cadence == TELECIDE_TYPE_BOB) cur_cadence = c->cadence_num[i + 1];
		}
		get_ref_string(cur_cadence, cadence_str);
		json_object *cadence = json_object_new_object();
		json_object *start = json_object_new_int(start_frame);
		json_object *end = json_object_new_int(end_frame / 2 - 1);
		json_object *ref = json_object_new_string(*cadence_str);
		json_object *bob_60mc_ref= json_object_new_int(-1);
		if (enable_bob && cur_cadence == TELECIDE_TYPE_BOB) {
			//TODO 前か後ろか決めるのは引数にする？(基本は後周期)
			if (i == c->cadence_count - 1) {
				bob_ivtc60mc_ref = c->cadence_num[i - 1];
			} else {
				bob_ivtc60mc_ref = c->cadence_num[i + 1];
			}
		}
		if (c->cadence_num[i] == TELECIDE_TYPE_BOB) bob_60mc_ref = json_object_new_int(bob_ivtc60mc_ref);
		json_object_object_add(cadence, "start", start);
		json_object_object_add(cadence, "end", end);
		json_object_object_add(cadence, "type", ref);
		if (c->cadence_num[i] == TELECIDE_TYPE_BOB && enable_bob) json_object_object_add(cadence, "txt60mc_type", bob_60mc_ref);
		json_object_array_add(cadence_list, cadence);
		if (i != c->cadence_count - 1) start_frame = c->cadence_index[i + 1] / 2;
	}
	//create comb data
	for (i = 0 ;i < c->comb_count; i++) {
		comb_data cm = c->comb[i];
		get_ref_string(cm.comb_current_cadence,cur_cadence_str);
		json_object *comb = json_object_new_object();
		json_object *frame = json_object_new_int(cm.comb_frame);
		json_object *current_ref = json_object_new_string(*cur_cadence_str);
		json_object_object_add(comb, "frame", frame);
		json_object_object_add(comb, "type", current_ref);
		json_object_array_add(comb_list, comb);
	}
	json_object_object_add(output, "cadence", cadence_list);
	json_object_object_add(output, "comb", comb_list);
	return 1;
}
//}}}

//
//json書き込み
//
int write_json(const char *filename, json_object *obj) { //{{{
	FILE *fp;
	fp = fopen(filename, "wt");
	if (!fp) {
		fprintf(stderr, "出力ファイルオープンエラー\n");
		return 0;
	}
	fprintf(fp, "%s", json_object_to_json_string(obj));
	fclose(fp);
	return 1;
}
//}}}

//
//ヘルプ
//
void show_help() { //{{{
	debug_print(DEBUG_INFO, "./autovfr -i [autovfr logfile]\n");
	debug_print(DEBUG_INFO, "オプション\n");
	debug_print(DEBUG_INFO, "-i [filename]\t\t入力AutoVFRログファイルパス\n");
	debug_print(DEBUG_INFO, "-o [filename]\t\t出力JSONファイル\n");
	debug_print(DEBUG_INFO, "--bob\t\t\tBOBを有効にします(テロップ,ANIME+がある場合は必須)\n\n");
	debug_print(DEBUG_INFO, "* 高度なオプション(全体的に試験的、かつ取り扱い注意)\n");
	debug_print(DEBUG_INFO, "--comb-num [num]\t60iとなる縞数を指定します。[%d]\n", detect_comb_num);
	debug_print(DEBUG_INFO, "--skip [num]\t\tスキップ数[%d]\n", seq_threshold);
	debug_print(DEBUG_INFO, "--verbose [num]\t\t詳細なログを表示します[%d]\n", verbose);
}
//}}}

//
//引数解析
//
int parse_args(int argc, char** argv) { //{{{
	int i;
	for (i = 1; i < argc; i++) {
		int next = 0;
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case '-':
					//long opts
					if (strcmp(argv[i] + 2, "skip") == 0) {
						if (i == (argc - 1)) {
							fprintf(stderr, "引数が違います。\n");
							return 0;
						}
						if (strtol(argv[i + 1], (char **) NULL, 10) != 0) {
							seq_threshold = strtol(argv[i + 1], (char **) NULL, 10);
						}
					} else if (strcmp(argv[i] + 2, "verbose") == 0) {
						if (i == (argc - 1)) {
							fprintf(stderr, "引数が違います。\n");
							return 0;
						}
						if (strtol(argv[i + 1], (char **) NULL, 10) != 0) {
							verbose = strtol(argv[i + 1], (char **) NULL, 10);
						}
					} else if (strcmp(argv[i] + 2, "bob") == 0 ) {
						enable_bob = 1;
					} else if (strcmp(argv[i] + 2, "comb-num") == 0) {
						if (i == (argc - 1)) {
							fprintf(stderr, "引数が違います。\n");
							return 0;
						}
						if (strtol(argv[i + 1], (char **) NULL, 10) != 0) {
							detect_comb_num = strtol(argv[i + 1], (char **) NULL, 10);
						}
					}
					break;
				case 'i':
					if (i == (argc - 1)) {
						fprintf(stderr, "引数が違います。\n");
						return 0;
					}
					if (!logfile) {
						logfile = argv[i + 1];
						fprintf(stdout, "input:%s\n", logfile);
					}
					break;
				case 'o':
					if (i == (argc - 1)) {
						fprintf(stderr, "引数が違います。\n");
						return 0;
					}
					if (!jsonfile) {
						jsonfile = argv[i + 1];
						fprintf(stdout, "output:%s\n", jsonfile);
					}
					break;
				default:
					fprintf(stderr, "Invalid argument.\n");
					return 0;
			}
			if (next) i++;
		}
	}
	return 1;
}
//}}}

//
//BOBフレームパターンを確認します。
//連続する縞数detect_comb_num以上でBOBと判定します。
//
int _check_comb_pattern(int *d) { //{{{
	int i;
	int count = 0;
	int bits = 0;
	for (i = 0; i < 10; i++) {
		if (i == 0 || count == 0) bits = d[i];
		bits &= d[i];
		if (bits) count++;
		if (!bits) count = 0;
		if (count >= detect_comb_num) {
			debug_print(DEBUG_HUGE, "comb detected ");
			return 1;
		}
	}
	return 0;
}
//}}}

//
//ログファイルを解析して5フレーム(=10フィールド)ごとに周期リストを作成します。
//
int _get_cadence(const int index, int *d, pair_data *p) { //{{{
	int cur = -1;
	int pair_a = -1;
	int pair_b = -1;
	debug_print(DEBUG_SHOW_COMB, " %d%d%d%d%d%d%d%d%d%d ", d[index], d[index + 1], d[index + 2], d[index + 3], d[index + 4], d[index + 5], d[index + 6], d[index + 7], d[index + 8], d[index + 9]);
	if (memcmp(d + index, blank_cadence, sizeof(int) * 10) == 0) {
		//no move.
		cur = TELECIDE_TYPE_NONE;
		pair_a = cur; pair_b = cur;
	} else if (memcmp(d + index, telop_cadence, sizeof(int) * 10) == 0) {
		//TELOP.
		cur = TELECIDE_TYPE_BOB;
		pair_a = cur; pair_b = cur;
	} else if (memcmp(d + index, deint01_cadence, sizeof(int) * 10) == 0 || memcmp(d + index, deint02_cadence, sizeof(int) * 10) == 0) {
		//deint (full detected)
		debug_print(DEBUG_HUGE, " deint ");
		cur = TELECIDE_TYPE_DINT;
		pair_a = cur; pair_b = cur;
	} else if (memcmp(d + index, deint_half_cadence, sizeof(int) * 6) == 0) {
		//deint (half detected)
		debug_print(DEBUG_HUGE, " deint ");
		cur = TELECIDE_TYPE_DINT;
		pair_a = cur; pair_b = cur;
	} else if (d[index] || d[index + 5]) {
		//may 3 or 4
		pair_a = TELECIDE_TYPE_REF3; pair_b = TELECIDE_TYPE_REF4;
		if (d[index + 2] || d[index + 7]) {
			//4
			cur = pair_b;
		} else if (d[index + 3] || d[index + 8]) {
			//3
			cur = pair_a;
		}
	} else if (d[index + 1] || d[index + 6]) {
		//may 1 or 2
		pair_a = TELECIDE_TYPE_REF1; pair_b = TELECIDE_TYPE_REF2;
		if (d[index + 3] || d[index + 8]) {
			//2
			cur = pair_b;
		} else if (d[index + 4] || d[index + 9]) {
			//1
			cur = pair_a;
		}
	} else if (d[index + 2] || d[index + 7]) {
		//may 0 or 4
		pair_a = TELECIDE_TYPE_REF0; pair_b = TELECIDE_TYPE_REF4;
		if (d[index] || d[index + 5]) {
			//4
			cur = pair_b;
		} else if (d[index + 4] || d[index + 9]) {
			//0
			cur = pair_a;
		}
	} else if (d[index + 3] || d[index + 8]) {
		//may 2 or 3
		pair_a = TELECIDE_TYPE_REF2; pair_b = TELECIDE_TYPE_REF3;
		if (d[index] || d[index + 5]) {
			//3
			cur = pair_b;
		} else if(d[index + 1] || d[index + 6]) {
			//2
			cur = pair_a;
		}
	} else if (d[index + 4] || d[index + 9]) {
		//may 0 or 1
		pair_a = TELECIDE_TYPE_REF0; pair_b = TELECIDE_TYPE_REF1;
		if (d[index + 1] || d[index + 6]) {
			//1
			cur = pair_b;
		} else if (d[index + 2] || d[index + 7]) {
			//0
			cur = pair_a;
		}
	} else {
		//unknown?
		debug_print(DEBUG_INFO, "[[%d Unknown]]\n", index);
	}
	if (_check_comb_pattern(d + index)) {
		cur = TELECIDE_TYPE_BOB;
		pair_a = cur; pair_b = cur;
	}
	debug_print(DEBUG_HUGE, "[[%d - (%d:%d) = %d]]\n", index, pair_a, pair_b, cur);
	p->pair_a = pair_a;
	p->pair_b = pair_b;
	p->result_cadence = cur;
	return 0;
}
//}}}

//
//2015.08.22版アルゴリズム(大まかな解析)
//シーンチェンジ1000まで
//
int analyse_cadence(frame_data *d, cadence_data *c) { //{{{
	int i;
	int dw_blocks = d->dw_blocks;
	int current_cadence = -1;
	int before_cadence  = -1;
	int detect_cadence  = -1;
	int max_key = -1;
	int max_val = -1;
	pair_data *pd = d->pair;
	int pair_seq_threshold = 3; //check pair count = seq_threshold + pair_seq_threshold
	int pair_max_skip_value = 2; //skip missing pair
	int seq_count = 0;
	int seq_count_lock = 0;
	int missing_flag = 0;
	int pair_counter[] = {0,0,0,0,0};
	int p_count = 0;
	int changed_count = 0;
	int changed_index[1000] = {0};
	int changed_cadence[1000] = {0};

	int detected_index = 0;

	for (i = 0; i < dw_blocks; i++) {
		max_key = -1; max_val = -1;
		pair_data before_data;
		//pair_data next_data;
		pair_data cur_data = pd[i];
		current_cadence = cur_data.result_cadence;
		if (i != 0) {
			before_data = pd[i - 1];
			before_cadence = before_data.result_cadence;
		}

		if (detect_cadence != -1 && current_cadence == TELECIDE_TYPE_NONE) current_cadence = detect_cadence;
		if (detect_cadence != -1 && before_cadence == TELECIDE_TYPE_NONE) before_cadence = detect_cadence;
		if (detect_cadence != -1 && current_cadence == -1) {
			// check pair //
			if (detect_cadence == cur_data.pair_a) current_cadence = cur_data.pair_a;
			if (detect_cadence == cur_data.pair_b) current_cadence = cur_data.pair_b;
			if (detect_cadence != cur_data.pair_a && detect_cadence != cur_data.pair_b) {
				debug_print(DEBUG_DEBUG, "%d pair missing. [%d:%d:%d:%d:%d]\n", i, pair_counter[0], pair_counter[1], pair_counter[2], pair_counter[3], pair_counter[4]);
				missing_flag = 1;
			}
			if (p_count >= seq_threshold + pair_seq_threshold) {
				get_max_pair(pair_counter, &max_key, &max_val);
				if (detect_cadence != max_key && max_key != TELECIDE_TYPE_NONE && max_val > pair_max_skip_value) {
					debug_print(DEBUG_DEBUG, "%d(%d) detected(pair) %d -> %d\n", i, detected_index, detect_cadence, max_key);
					changed_index[changed_count] = detected_index * 10;
					changed_cadence[changed_count] = max_key;
					changed_count++;
					detect_cadence = max_key;
					current_cadence = detect_cadence;
				}
				memset(pair_counter, 0, sizeof(int) * 5);
				missing_flag = 0;
				p_count = 0;
				seq_count = 0;
				seq_count_lock = 1;
			}
		}

		if (detect_cadence != current_cadence && seq_count_lock) {
			debug_print(DEBUG_DEBUG, "%d changed?\n", i);
			detected_index = i;
			seq_count = 0;
			seq_count_lock = 0;
		}
		debug_print(DEBUG_DEBUG, "%d [%d:%d] = %d b:%d c:%d seq:%d (%d)\n", i, cur_data.pair_a, cur_data.pair_b, cur_data.result_cadence, before_cadence, current_cadence, seq_count, detect_cadence);

		if (current_cadence != -1 && current_cadence == before_cadence && seq_count_lock == 0) {
			seq_count++;
			if (seq_count >= seq_threshold) {
				if (detect_cadence != current_cadence && current_cadence != TELECIDE_TYPE_NONE) {
					debug_print(DEBUG_DEBUG, "%d(%d) detected %d -> %d\n", i, detected_index, detect_cadence, current_cadence);
					detect_cadence = current_cadence;
					if (changed_count == 0) detected_index = 0;
					changed_index[changed_count] = detected_index * 10;
					changed_cadence[changed_count] = current_cadence;
					changed_count++;
					memset(pair_counter, 0, sizeof(int) * 5);
					missing_flag = 0;
					p_count = 0;
				}
				seq_count = 0;
				seq_count_lock = 1;
			}
		} else if (seq_count_lock == 0) {
			seq_count = 0;
			seq_count_lock = 0;
		}
		if (missing_flag && current_cadence != TELECIDE_TYPE_NONE) {
			pair_counter[cur_data.pair_a]++;
			pair_counter[cur_data.pair_b]++;
			p_count++;
		}
	}
	c->cadence_count = changed_count;
	c->cadence_index = malloc(sizeof(int) * changed_count);
	c->cadence_num = malloc(sizeof(int) * changed_count);
	for (i = 0; i < changed_count; i++) {
		memcpy(c->cadence_index + i, changed_index + i, sizeof(int));
		memcpy(c->cadence_num + i, changed_cadence + i, sizeof(int));
	}
	return 0;
}
//}}}

//
//2015.08.24版
//
int _analyse_sima(frame_data *d, cadence_data *c) { //{{{
	int i;
	int j = 0;
	int cur_cadence = -1;
	int k = 0;
	int frame_count = 0;
	int c_size = -1;
	comb_data *comb_newptr = NULL;
	for (i = 0; i < d->total_dwframes; i+=10) {
		if (i >= c->cadence_index[j + 1] && j + 1 < c->cadence_count)
			j++;
		cur_cadence = c->cadence_num[j];
		pair_data pd = d->pair[k];
		int *pos = d->comb_list + i;
		int comb_pattern[4] = {0};
		int is_comb = 0;
		int l = 0;
		if (pd.result_cadence != TELECIDE_TYPE_DINT && pd.result_cadence != TELECIDE_TYPE_BOB) {
			if (cur_cadence == TELECIDE_TYPE_REF0 && (pos[0] || pos[3] || pos[6] || pos[8])) {
				is_comb = 1;
				comb_pattern[0] = pos[0]; comb_pattern[1] = pos[3];
				comb_pattern[2] = pos[6]; comb_pattern[3] = pos[8];
			} else if (cur_cadence == TELECIDE_TYPE_REF1 && (pos[0] || pos[2] || pos[5] || pos[8])) {
				is_comb = 1;
				comb_pattern[0] = pos[0]; comb_pattern[1] = pos[2];
				comb_pattern[2] = pos[5]; comb_pattern[3] = pos[8];
			} else if (cur_cadence == TELECIDE_TYPE_REF2 && (pos[0] || pos[2] || pos[4] || pos[7])) {
				is_comb = 1;
				comb_pattern[0] = pos[0]; comb_pattern[1] = pos[2];
				comb_pattern[2] = pos[4]; comb_pattern[3] = pos[7];
			} else if (cur_cadence == TELECIDE_TYPE_REF3 && (pos[2] || pos[4] || pos[6] || pos[9])) {
				is_comb = 1;
				comb_pattern[0] = pos[2]; comb_pattern[1] = pos[4];
				comb_pattern[2] = pos[6]; comb_pattern[3] = pos[9];
			} else if (cur_cadence == TELECIDE_TYPE_REF4 && (pos[1] || pos[4] || pos[6] || pos[8])) {
				is_comb = 1;
				comb_pattern[0] = pos[1]; comb_pattern[1] = pos[4];
				comb_pattern[2] = pos[6]; comb_pattern[3] = pos[8];
			}
		}
		debug_print(DEBUG_DEBUG, "%d(%d) %d%d%d%d%d%d%d%d%d%d \n", i, frame_count, pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pos[6], pos[7], pos[8], pos[9]);
		for (l = 0; l < 4; l++) {
			if (is_comb && comb_pattern[l]) {
				if (c->comb_count >= c_size) {
					c_size += 512;
					if (c->comb_count == 0) {
						c->comb = malloc(sizeof(comb_data) * c_size);
					} else {
						comb_newptr = realloc(c->comb, sizeof(comb_data) * c_size);
						if (comb_newptr == NULL) {
							debug_print(DEBUG_INFO, "Can't reallocate memory.\n");
							return 1;
						}
						c->comb = comb_newptr;
					}
				}
				comb_data *comb = malloc(sizeof(comb_data));
				comb->comb_frame = frame_count + l;
				comb->comb_current_cadence = cur_cadence;
				debug_print(DEBUG_DEBUG, "%d comb (%d)\n", frame_count + l, cur_cadence);
				memcpy(c->comb + c->comb_count, comb, sizeof(comb_data));
				c->comb_count++;
			}
		}
		frame_count += 4;
		k++;
	}
	if (comb_newptr != NULL) {
		free(comb_newptr);
	}

	return 0;
}
//}}}

//
//旧判定コード
//Deprecated
int analyse_sima(int index, int *d, int cur_cadence, comb_data *comb) { //{{{
	int cur = -1;
	int has_pair = 0;
	int pair_a = -1;
	int pair_b = -1;
	int is_comb = 0;

	if (memcmp(d + index, blank_cadence, sizeof(int) * 10) == 0) {
		cur = cur_cadence;
	} else if (memcmp(d + index, telop_cadence, sizeof(int) * 10) == 0) {
		cur = 5;
	} else if ((d[index + 2] && d[index + 4]) || (d[index + 7] && d[index + 9])) {
		cur = 0;
	} else if ((d[index + 1] && d[index + 4]) || (d[index + 6] && d[index + 9])) {
		cur = 1;
	} else if ((d[index + 1] && d[index + 3]) || (d[index + 6] && d[index + 8])) {
		cur = 2;
	} else if ((d[index    ] && d[index + 3]) || (d[index + 5] && d[index + 8])) {
		cur = 3;
	} else if ((d[index    ] && d[index + 2]) || (d[index + 5] && d[index + 7])) {
		cur = 4;
	} else if (d[index    ] || d[index + 5]) {
		has_pair = 1;
		pair_a = 3; pair_b = 4;
	} else if (d[index + 1] || d[index + 6]) {
		has_pair = 1;
		pair_a = 1; pair_b = 2;
	} else if (d[index + 2] || d[index + 7]) {
		has_pair = 1;
		pair_a = 0; pair_b = 4;
	} else if (d[index + 3] || d[index + 8]) {
		has_pair = 1;
		pair_a = 2; pair_b = 3;
	} else if (d[index + 4] || d[index + 9]) {
		has_pair = 1;
		pair_a = 0; pair_b = 1;
	}
	
	if (cur == cur_cadence) {
		if (cur_cadence == 5 && enable_bob == 1) {
			//debug_print(DEBUG_DEBUG, "%d BOB cur(%d)\n", index, c->cadence_num[c_index - 2]);
		} else {
			debug_print(DEBUG_DEBUG, "%d ref(%d)\n", index, cur_cadence);
		}
	} else {
		if (has_pair) {
			if (pair_a != cur_cadence && pair_b != cur_cadence) {
				debug_print(DEBUG_DEBUG, "%d Comb ref(%d) pair missing[%d:%d]\n", index,cur_cadence, pair_a, pair_b);
				is_comb = 1;
			}
		} else {
			if (cur == 5 && enable_bob == 0) {
				debug_print(DEBUG_DEBUG, "%d BOB ref(%d)\n", index, cur_cadence);
			} else {
				debug_print(DEBUG_DEBUG, "%d Comb ref(%d) pattern(%d)\n", index, cur_cadence, cur);
				is_comb = 1;
			}
		}
	}
	//create SIMA data.
	comb->comb_frame = index;
	comb->comb_current_cadence = cur_cadence;
	/*
	if (has_pair) {
		comb->comb_ref = -1;
		comb->comb_has_pair = has_pair;
		comb->comb_pair_a = pair_a;
		comb->comb_pair_b = pair_b;
	} else {
		comb->comb_ref = cur;
		comb->comb_has_pair = 0;
		comb->comb_pair_a = -1;
		comb->comb_pair_b = -1;
	}
	*/
	return is_comb;
}
//}}}

int main(int argc, char** argv) {
	int ret;
	int i;
	frame_data *data;
	cadence_data *c_data;
	c_data = malloc(sizeof(cadence_data));
	data = malloc(sizeof(frame_data));
	if (argc < 2) {
		show_help();
		return 0;
	}
	parse_args(argc, argv);
	ret = read_file(logfile, data);
	if (!ret) {
		fprintf(stderr, "AutoVFRログファイルを読み込めませんでした。\n");
		free(c_data);
		free_data(data);
		return 2;
	}
	fprintf(stdout, "Total frames:%ld\n", data->total_frames);
	fprintf(stdout, "Total dw frames:%ld\n", data->total_dwframes);
	data->dw_blocks = data->total_dwframes / 10;
	data->pair = malloc(sizeof(pair_data) * data->dw_blocks);

	int j = 0;
	for (i = 0; i < data->dw_size; i+=10) {
		_get_cadence(i, data->comb_list, data->pair + j);
		j++;
	}
	analyse_cadence(data, c_data);
	c_data->comb_count = 0;
	c_data->comb = malloc(sizeof(comb_data));
	_analyse_sima(data, c_data);
	fprintf(stdout, "-----------周期リスト----------\n");
	int cur_cadence = -1;
	int start_frame = 0;
	int end_frame = -1;
	for (i = 0; i < c_data->cadence_count; i++) {
		cur_cadence = c_data->cadence_num[i];
		if (i == c_data->cadence_count - 1) {
			end_frame = data->total_dwframes / 2 - 1;
			if (!enable_bob && cur_cadence == TELECIDE_TYPE_BOB) cur_cadence = c_data->cadence_num[i - 1];
		} else {
			end_frame = c_data->cadence_index[i + 1] / 2 - 1;
			if (!enable_bob && cur_cadence == TELECIDE_TYPE_BOB) cur_cadence = c_data->cadence_num[i + 1];
		}
		fprintf(stdout, "pcadence:%d frame:%d-%d\n", cur_cadence, start_frame, end_frame);
		if (i != c_data->cadence_count - 1) start_frame = c_data->cadence_index[i + 1] / 2;
	}
	fprintf(stdout, "----------縞リスト----------\n");
	for (i = 0; i < c_data->comb_count; i++) {
		fprintf(stdout, "pcadence:%d comb:%d\n", c_data->comb[i].comb_current_cadence, c_data->comb[i].comb_frame);
	}
	json_object *object = json_object_new_object();
	create_json(data, c_data, object);
	if (object && jsonfile) {
		ret = write_json(jsonfile, object);
		if (ret)
			fprintf(stdout, "JSONファイルに書き込みました。\n");
	}
	free(c_data->comb);
	free(c_data->cadence_num);
	free(c_data->cadence_index);
	free(c_data);
	free_data(data);

	return 1;
}
