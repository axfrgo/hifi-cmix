#ifndef ARTICLE_REORDER_H 
#define ARTICLE_REORDER_H 

#include <algorithm>
#include <string.h>
#include <string>
#include <vector>
#include <cstdio>
#include <unordered_map>
#include <fstream>

#define NUM_OF_ARTICLES 243425

struct Accumulator {
  int id;
  int start;
  int end;
  std::string sort_key;
   #ifdef DUMPARTICLE
  std::string title;
  std::string infobox;
  std::string redirect;
  #endif
};

enum ParserState {
  expect_page = 0,
  expect_id,
  expect_pageend
};

int line_count = 0;
static char s[8192*8];

void sort_by_page_id(std::vector<Accumulator>& pages) {
    std::sort(pages.begin(), pages.end(),
              [](const Accumulator& left, const Accumulator& right) {
                  return left.id < right.id;
              });
}

const char *patterns1[] = { "<page>", "<id>", "</page>" };
const int transitions1[3] = { expect_id, expect_pageend, expect_page };
std::vector<Accumulator> vec;
std::vector<std::string> lines;
// in phda9
int wfgets(char *str, int count, FILE  *fp);
void wfputs(const char *str, FILE *fp);

void loadFile(const char *fname) {
    line_count = 0;
    int state = expect_page;
    Accumulator acc;
    std::string curr_title;
    std::string curr_target;
    FILE* file = fopen(fname, "rb");
    while (wfgets(s, 8192*8, file)) {
        #ifdef DUMPARTICLE
        char *pt = strstr(s, "<title>");
        if (pt) acc.title=s,acc.title.pop_back();
        if (strstr(s, "infobox ") || strstr(s, "infobox_")  || strstr(s, "Infobox_") || strstr(s, "Infobox ")) acc.infobox=s,acc.infobox.pop_back();
        if (strstr(s, "#REDIRECT") || strstr(s, "#redirect") || strstr(s, "softredirect")|| strstr(s, "#Redirect")|| strstr(s, "#REdirect")) acc.redirect=s,acc.redirect.pop_back();
        #endif

        if (curr_title.empty()) {
            char *pt = strstr(s, "<title>");
            if (pt) {
                char *et = strstr(pt + 7, "</title>");
                if (et) {
                    curr_title.assign(pt + 7, et - (pt + 7));
                }
            }
        }
        if (curr_target.empty()) {
            char *pr = strstr(s, "#REDIRECT");
            if (!pr) pr = strstr(s, "#redirect");
            if (!pr) pr = strstr(s, "#Redirect");
            if (!pr) pr = strstr(s, "#REdirect");
            if (pr) {
                char *b1 = strstr(pr, "[[");
                if (b1) {
                    char *b2 = strstr(b1 + 2, "]]");
                    if (b2) {
                        curr_target.assign(b1 + 2, b2 - (b1 + 2));
                    }
                }
            } else {
                char *ps = strstr(s, "softredirect");
                if (ps) {
                    char *b1 = strchr(ps, '|');
                    if (b1) {
                        char *b2 = strstr(b1 + 1, "}}");
                        if (b2) {
                            curr_target.assign(b1 + 1, b2 - (b1 + 1));
                        }
                    }
                }
            }
        }

        char *p = strstr(s, patterns1[state]);
        if (p) {
            if (state == expect_page) {
                acc.start = line_count;
                curr_title.clear();
                curr_target.clear();
            } else if (state == expect_id) {
                char *p = strstr(s, patterns1[1]); //id
                if (p) {
                    p = p + 4;
                    acc.id = atoi(p);
                }
            } else if (state == expect_pageend) {
                acc.end = line_count;
            }
            state = transitions1[state];
            if (state == expect_page) {
                if (!curr_target.empty()) {
                    acc.sort_key = "R:" + curr_target;
                } else if (!curr_title.empty()) {
                    acc.sort_key = "T:" + curr_title;
                } else {
                    acc.sort_key.clear();
                }
                vec.push_back(acc);
                #ifdef DUMPARTICLE
                acc.title="",acc.infobox="",acc.redirect="";
                #endif
            }
        } 
        line_count++;
        std::string so = std::string(s);
        lines.push_back(so);
    }
    fclose(file);
}

void reorder() {
    std::vector<int> positions;
    int total_articles = (int)NUM_OF_ARTICLES;
    
    loadFile(".main");
    if ((int)vec.size() < total_articles) {
        total_articles = (int)vec.size();
    }
    std::vector<int> used(total_articles, 0);

    // Article numbers need to be remapped (see article_remap.cpp).
    // Here we read enwik9 in order to generate the article number mapping.
    std::unordered_map<int, int> remap;
    std::ifstream infile(".main");
    std::string line;
    int count1 = -1, count2 = -1;
    bool redirect = false;
    std::vector<std::string> prefix = {
      "      <text xml:space=\"preserve\">#REDIRECT",
      "      <text xml:space=\"preserve\">#redirect",
      "      <text xml:space=\"preserve\">#Redirect",
      "      <text xml:space=\"preserve\">#REdirect",
      "      <text xml:space=\"preserve\">{{softredirect",};
    while (getline(infile, line)) {
      for (auto pre : prefix) {
        if (line.rfind(pre, 0) == 0) {
          redirect = true;
          break;
        }
      }
      if (line == "  <page>") {
        remap[count2] = count1;
        if (!redirect) {
          ++count2;
        }
        ++count1;
        redirect = false;
      }
    }

    std::ifstream infile2(".new_article_order");
    while (getline(infile2, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
      }
      if (line.empty()) continue;
      char* endptr = nullptr;
      long val = strtol(line.c_str(), &endptr, 10);
      if (endptr != line.c_str()) {
        int res = remap[(int)val];
        if (res >= 0 && res < total_articles) {
          positions.push_back(res);
          used[res] = 1;
        }
      }
    }
  
    if ((int)positions.size() < total_articles) {
        std::vector<int> remaining;
        for (int i = 0; i < total_articles; i++) {
            if (used[i] == 0) {
                remaining.push_back(i);
            }
        }
        std::stable_sort(remaining.begin(), remaining.end(),
            [](int a, int b) {
                return vec[a].sort_key < vec[b].sort_key;
            });
        for (int idx : remaining) {
            positions.push_back(idx);
        }
    }
			  
    FILE* out = fopen(".main_reordered", "wb");
    std::string so;
    for (size_t i = 0; i < positions.size(); i++) {
      int pos = positions[i];
      #ifdef DUMPARTICLE
      printf("%d\t%d\t%d\t%d\t%s\t%s\t%s\n",pos,vec[pos].id,vec[pos].start,vec[pos].end,vec[pos].title.c_str(),vec[pos].infobox.c_str(),vec[pos].redirect.c_str());
      #else
      for (int j = vec[pos].start; j <= vec[pos].end; j++) {
        so = lines[j];
        wfputs(so.c_str(), out);
      }
      #endif
    } 
    #ifdef DUMPARTICLE
    exit(0);
    #endif
    fclose(out);
    vec.clear();
    std::vector<Accumulator>(vec).swap(vec);  // Why C++? why
    lines.clear();
    std::vector<std::string>(lines).swap(lines);
}

void sort() {
    loadFile(".main_decomp_restored");
 
    sort_by_page_id(vec);
	  
    FILE* out = fopen(".main_decomp_restored_sorted", "wb");
    std::string so;
    for (size_t i = 0; i < vec.size(); i++) {
        for (int j = vec[i].start; j <= vec[i].end; j++) {
            so = lines[j];
            wfputs(so.c_str(), out);
        }
    }
  
    fclose(out);
    vec.clear();
    std::vector<Accumulator>(vec).swap(vec);
    lines.clear(); 
    std::vector<std::string>(lines).swap(lines); 
}

#endif // ARTICLE_REORDER_H
