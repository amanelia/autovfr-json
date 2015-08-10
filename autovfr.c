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
	unsigned long total_frames;
	unsigned long total_dwframes;
	unsigned long dw_size;
	int *comb_list;
	int *crop_comb_list;
	int *move_comb_list;
} frame_data;

const int blank_cadence[] = {0,0,0,0,0,0,0,0,0,0};
const int telop_cadence[] = {1,1,1,1,1,1,1,1,1,1};
const int deint_cadence[] = {0,1,0,1,0,1,0,1,0,1};

int current_cadence = -1;
int before_cadence  = -1;
int count = 0;
int count_ok = 0;
int bob_flag = 0;
int last_index_number = 0;

int pair_count[] = {0, 0, 0, 0, 0};
int p_count = 0;

typedef struct {
	int comb_frame;
	int comb_current_cadence;
	int comb_ref;
	/* Pair */
	int comb_has_pair;
	int comb_pair_a;
	int comb_pair_b;
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

int skip_num = 1;
int skip_pair = 3;
int enable_bob = 0;
int bob_area_frames = 0;

int verbose = 0;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define DEBUG_INFO 0
#define DEBUG_DEBUG 1
#define DEBUG_HUGE 2

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
	free(d->comb_list);
	free(d->crop_comb_list);
	free(d->move_comb_list);
	free(d);
}

int read_file(const char *filename, frame_data *d) {
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

void get_ref_string(const int ref_type, char **ref_str) {
	switch (ref_type) {
		case 0:
			*ref_str = "pref0";
			break;
		case 1:
			*ref_str = "pref1";
			break;
		case 2:
			*ref_str = "pref2";
			break;
		case 3:
			*ref_str = "pref3";
			break;
		case 4:
			*ref_str = "pref4";
			break;
		case 5:
			*ref_str = "bob";
			break;
		default:
			*ref_str = "-";
			break;
	}
}

int create_json(const frame_data *f, const cadence_data *c, json_object *output) {
	int i;
	//new
	json_object *cadence_list = json_object_new_array();
	json_object *comb_list = json_object_new_array();
	int end_frame = 0;
	char *cadence_str[16];
	char *cur_cadence_str[16];
	char *diff_cadence_str[16];
	//create cadence data
	for (i = 0; i < c->cadence_count; i++) {
		if (i == c->cadence_count - 1) {
			end_frame = f->dw_size;
		} else {
			end_frame = c->cadence_index[i + 1];
		}
		get_ref_string(c->cadence_num[i], cadence_str);
		json_object *cadence = json_object_new_object();
		json_object *start = json_object_new_int(c->cadence_index[i] / 2);
		json_object *end = json_object_new_int(end_frame / 2 - 1);
		json_object *ref = json_object_new_string(*cadence_str);
		json_object_object_add(cadence, "start", start);
		json_object_object_add(cadence, "end", end);
		json_object_object_add(cadence, "type", ref);
		json_object_array_add(cadence_list, cadence);
	}
	//create comb data
	for (i = 0 ;i < c->comb_count; i++) {
		comb_data cm = c->comb[i];
		get_ref_string(cm.comb_current_cadence,cur_cadence_str);
		json_object *comb = json_object_new_object();
		json_object *frame = json_object_new_int(cm.comb_frame / 2);
		json_object *current_ref = json_object_new_string(*cur_cadence_str);
		json_object *b_pair = json_object_new_boolean(cm.comb_has_pair);
		*diff_cadence_str = "-";
		json_object *diff_ref = json_object_new_string("-");
		json_object *i_pair_a = json_object_new_int(-1);
		json_object *i_pair_b = json_object_new_int(-1);
		if (cm.comb_has_pair) {
			i_pair_a = json_object_new_int(cm.comb_pair_a);
			i_pair_b = json_object_new_int(cm.comb_pair_b);
		} else {
			get_ref_string(cm.comb_ref, diff_cadence_str);
			diff_ref = json_object_new_string(*diff_cadence_str);
		}
		json_object_object_add(comb, "frame", frame);
		json_object_object_add(comb, "type", current_ref);
		json_object_object_add(comb, "have-pair", b_pair);
		json_object_object_add(comb, "ref", diff_ref);
		if (cm.comb_has_pair) {
			json_object_object_add(comb, "pair-a", i_pair_a);
			json_object_object_add(comb, "pair-b", i_pair_b);
		}
		json_object_array_add(comb_list, comb);
	}
	json_object_object_add(output, "cadence", cadence_list);
	json_object_object_add(output, "comb", comb_list);
	return 1;
}

int write_json(const char *filename, json_object *obj) {
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

void show_help() {
	debug_print(DEBUG_INFO, "./autovfr -i [autovfr logfile]\n");
	debug_print(DEBUG_INFO, "オプション\n");
	debug_print(DEBUG_INFO, "-i [filename]\t\t入力AutoVFRログファイルパス\n");
	debug_print(DEBUG_INFO, "-o [filename]\t\t出力JSONファイル\n");
	debug_print(DEBUG_INFO, "--skip [num]\t\tスキップ数[%d](*** DOESN'T WORK ***)\n", skip_num);
	debug_print(DEBUG_INFO, "--verbose [num]\t\t詳細なログを表示します[%d]\n", verbose);
	debug_print(DEBUG_INFO, "--bob\t\t\tBOBを有効にします(テロップ,ANIME+がある場合は必須)\n");
	debug_print(DEBUG_INFO, "--bob-area [num]\tBOBの範囲を広げます[%d](*** DOESN'T WORK ***)\n", bob_area_frames);
}

int parse_args(int argc, char** argv) {
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
							skip_num = strtol(argv[i + 1], (char **) NULL, 10);
							skip_pair = skip_num + 2;
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
					} else if (strcmp(argv[i] + 2, "bob-area") == 0) {
						if (i == (argc - 1)) {
							fprintf(stderr, "引数が違います。\n");
							return 0;
						}
						if (strtol(argv[i + 1], (char **) NULL, 10) != 0) {
							bob_area_frames = strtol(argv[i + 1], (char **) NULL, 10);
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

int get_cadence(const int index, int *d, int *changed_index, int *changed_cadence) {
	int cur = -1;
	int changed = 0;
	int pair_a = -1;
	int pair_b = -1;
	int max_key = -1;
	int max_val = -1;
	int has_pair = 0;
	int bob = 0;
	int no_move = 0;
	if (memcmp(d + index, blank_cadence, sizeof(int) * 10) == 0) {
		debug_print(DEBUG_HUGE, "%d - n before(%d)\n", index, before_cadence);
		if (current_cadence != -1) cur = current_cadence;
		no_move = 1;
	} else if (memcmp(d + index, telop_cadence, sizeof(int) * 10) == 0) {
		debug_print(DEBUG_HUGE, "%d - t\n", index);
		bob = 1;
		if (current_cadence != -1) cur = current_cadence;
	} else if ((d[index + 2] && d[index + 4]) || (d[index + 7] && d[index + 9])) {
		debug_print(DEBUG_HUGE, "%d - 0\n", index);
		cur = 0;
	} else if ((d[index + 1] && d[index + 4]) || (d[index + 6] && d[index + 9])) {
		debug_print(DEBUG_HUGE, "%d - 1\n", index);
		cur = 1;
	} else if ((d[index + 1] && d[index + 3]) || (d[index + 6] && d[index + 8])) {
		debug_print(DEBUG_HUGE, "%d - 2\n", index);
		cur = 2;
	} else if ((d[index    ] && d[index + 3]) || (d[index + 5] && d[index + 8])) {
		debug_print(DEBUG_HUGE, "%d - 3\n", index);
		cur = 3;
	} else if ((d[index    ] && d[index + 2]) || (d[index + 5] && d[index + 7])) {
		debug_print(DEBUG_HUGE, "%d - 4\n", index);
		cur = 4;
	} else if (d[index] || d[index + 5]) {
		debug_print(DEBUG_HUGE, "%d - 3 or 4\n", index);
		has_pair = 1;
		pair_a = 3;
		pair_b = 4;
	} else if (d[index + 1] || d[index + 6]) {
		debug_print(DEBUG_HUGE, "%d - 1 or 2\n", index);
		has_pair = 1;
		pair_a = 3;
		pair_a = 1;
		pair_b = 2;
	} else if (d[index + 2] || d[index + 7]) {
		debug_print(DEBUG_HUGE, "%d - 0 or 4\n", index);
		has_pair = 1;
		pair_a = 3;
		pair_a = 0;
		pair_b = 4;
	} else if (d[index + 3] || d[index + 8]) {
		debug_print(DEBUG_HUGE, "%d - 2 or 3\n", index);
		has_pair = 1;
		pair_a = 3;
		pair_a = 2;
		pair_b = 3;
	} else if (d[index + 4] || d[index + 9]) {
		debug_print(DEBUG_HUGE, "%d - 0 or 1\n", index);
		has_pair = 1;
		pair_a = 3;
		pair_a = 0;
		pair_b = 1;
	} else {
		printf("%d - ------\n", index);
	}
	if (bob == 1 && bob_flag == 0) {
		bob_flag = 1;
		debug_print(DEBUG_DEBUG, "%d bob start.\n", index);
		if (enable_bob) {
			changed = 1;
			*changed_index = index;
			*changed_cadence = 5;
		}
	} else if (bob == 0 && bob_flag == 1) {
		bob_flag = 0;
		debug_print(DEBUG_DEBUG, "%d bob end.\n", index);
		if (enable_bob) {
			changed = 1;
			*changed_index = index;
			*changed_cadence = current_cadence;
		}
	}
	if (cur != -1 && before_cadence != cur && count_ok == 1) {
		debug_print(DEBUG_DEBUG, "*");
		count_ok = 0;
		count = 0;
		last_index_number = index;
	} else if (cur != -1 && before_cadence == cur && count_ok == 0) {
		count++;
	}
	if (count >= skip_num) {
		count_ok = 1;
		count = 0;
		if (current_cadence != cur) {
			debug_print(DEBUG_DEBUG, "%d Cadence %d -> %d\n", last_index_number, current_cadence, cur);
			current_cadence = cur;
			changed = 1;
			*changed_index = last_index_number;
			*changed_cadence = current_cadence;
		}
	}

	if (has_pair) {
		if (before_cadence == pair_a || before_cadence == pair_b) {
			cur = before_cadence;
		} else {
			if (current_cadence == pair_a || current_cadence == pair_b) {
				cur = current_cadence;
			} else {
				pair_count[pair_a]++;
				pair_count[pair_b]++;
				p_count++;
				if (p_count == 1) last_index_number = index;
				debug_print(DEBUG_DEBUG, "%d [%d:%d:%d:%d:%d]\n",index, pair_count[0], pair_count[1], pair_count[2], pair_count[3], pair_count[4]);
				if (p_count >= skip_pair) {
					get_max_pair(pair_count, &max_key, &max_val);
					debug_print(DEBUG_DEBUG, "%d Changed %d -> %d (count:%d)\n", last_index_number, current_cadence, max_key, max_val);
					p_count = 0;
					current_cadence = max_key;
					cur = max_key;
					changed = 1;
					*changed_index = last_index_number;
					*changed_cadence = current_cadence;
					memset(&pair_count, 0, sizeof(int) * 5);
				}
			}
		}
	} else {
		memset(&pair_count, 0, sizeof(int) * 5);
		p_count = 0;
	}
	if (!no_move)
		before_cadence = cur;

	return changed;
}

int analyse_sima(int index, int *d, int cur_cadence, comb_data *comb) {
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
	return is_comb;
}

void reset_cadence() {
	current_cadence = -1;
	before_cadence = -1;
	count_ok = 0;
	count = 0;
	last_index_number = 0;
}

int main(int argc, char** argv) {
	int ret;
	int i;
	int changed_count = 0;
	int changed_index = -1;
	int changed_cadence = -1;
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
	//dump
	fprintf(stdout, "----------周期リスト----------\n");
	for (i = 0; i < data->dw_size; i+=10) {
		ret = get_cadence(i, data->comb_list, &changed_index, &changed_cadence);
		if (ret) {
			changed_count++;
		}
	}
	c_data->cadence_count = changed_count;
	c_data->cadence_index = malloc(sizeof(int) * changed_count);
	c_data->cadence_num = malloc(sizeof(int) * changed_count);
	reset_cadence();
	int j = 0;
	for (i = 0; i < data->dw_size; i+=10) {
		ret = get_cadence(i, data->comb_list, &changed_index, &changed_cadence);
		if (ret) {
			c_data->cadence_index[j] = changed_index;
			c_data->cadence_num[j] = changed_cadence;
			if (j == 0) c_data->cadence_index[j] = 0;
			j++;
		}
	}
	fprintf(stdout, "周期数:%d\n", c_data->cadence_count);
	int next_frame = 0;
	for (i = 0; i < c_data->cadence_count; i++) {
		if (i == c_data->cadence_count - 1) {
			next_frame = data->dw_size;
		} else {
			next_frame = c_data->cadence_index[i + 1];
		}
		if (c_data->cadence_num[i] == 5) {
			fprintf(stdout, "%d-%d BOB\n", c_data->cadence_index[i] / 2, next_frame / 2 - 1);
		} else {
			fprintf(stdout, "%d-%d ref(%d)\n", c_data->cadence_index[i] / 2, next_frame / 2 - 1, c_data->cadence_num[i]);
		}
	}
	fprintf(stdout, "----------解除縞リスト----------\n");
	int comb_count = 0;
	int cur_index = 0;
	int current_cadence = -1;
	comb_data *comb = malloc(sizeof(comb));
	for (i = 0; i < data->dw_size; i+=10) {
		if(i >= c_data->cadence_index[cur_index + 1] && cur_index + 1 < c_data->cadence_count)
			cur_index++;
		current_cadence = c_data->cadence_num[cur_index];
		ret = analyse_sima(i, data->comb_list, current_cadence, comb);
		if (ret) comb_count++;
	}
	fprintf(stdout, "Comb: %d\n", comb_count);
	c_data->comb = malloc(sizeof(comb_data) * comb_count);
	c_data->comb_count = comb_count;
	cur_index = 0;
	current_cadence = -1;
	j = 0;
	for (i = 0; i < data->dw_size; i+=10) {
		if (i >= c_data->cadence_index[cur_index + 1] && cur_index + 1 < c_data->cadence_count)
			cur_index++;
		current_cadence = c_data->cadence_num[cur_index];
		ret = analyse_sima(i, data->comb_list, current_cadence, comb);
		if (ret) {
			memcpy(c_data->comb + j, comb, sizeof(comb_data));
			j++;
		}
	}
	for (i = 0; i < c_data->comb_count; i++) {
		comb_data c = c_data->comb[i];
		if (c.comb_has_pair) {
			fprintf(stdout, "%d Comb ref(%d) 周期候補[%d:%d]\n", c.comb_frame / 2, c.comb_current_cadence, c.comb_pair_a, c.comb_pair_b);
		} else {
			fprintf(stdout, "%d Comb ref(%d) 候補(%d)\n", c.comb_frame / 2, c.comb_current_cadence, c.comb_ref);
		}
	}
	json_object *object = json_object_new_object();
	create_json(data, c_data, object);
	if (object && jsonfile) {
		ret = write_json(jsonfile, object);
		if (ret) {
			fprintf(stdout, "JSONファイルに書き込みました\n");
		}
	}
	free(comb);
	free(c_data->comb);
	free(c_data->cadence_num);
	free(c_data->cadence_index);
	free(c_data);
	free_data(data);

	return 1;
}
