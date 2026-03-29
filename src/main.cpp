#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <climits>
#include <filesystem>
#include <fstream>

#include <unistd.h>
#include <getopt.h>

#define COL_RESET  "\033[0m"
#define COL_MATCH  "\033[01;31m" // bold red
#define COL_FILE   "\033[35m"    // magenta
#define COL_LNUM   "\033[32m"    // green
#define COL_SEP    "\033[36m"    // cyan

std::string BEG = "";
std::string END = "";

typedef enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } ColorMode;

typedef struct
{
  int extended;    // -E
  int perl;        // -P
  int only_match;  // -o
  int ignore_case; // -i
  int line_num;    // -n
  int invert;      // -v
  int count;       // -c
  int recursive;   // -r
  int follow_links;// -R
  char* pattern;
  char** files;
  int file_count;
  ColorMode color;
} GrepArgs;

GrepArgs parse_args(int argc, char* argv[])
{
  GrepArgs args = { 0 };
  args.color = COLOR_AUTO;
  int opt;

  static const struct option long_opts[] = {
        { "extended-regexp",        no_argument,       NULL, 'E' },
        { "color",                  optional_argument, NULL,  1  },
        { "colour",                 optional_argument, NULL,  1  },
        { NULL, 0, NULL, 0 }
  };

  while ((opt = getopt_long(argc, argv, "EPoinvcrRe:", long_opts, NULL)) != -1)
  {
    switch (opt)
    {
      case 'E': args.extended = 1; break;
      case 'P': args.perl = 1; break;
      case 'o': args.only_match = 1; break;
      case 'i': args.ignore_case = 1; break;
      case 'n': args.line_num = 1; break;
      case 'v': args.invert = 1; break;
      case 'c': args.count = 1; break;
      case 'r': args.recursive = 1; break;
      case 'R': args.recursive = 1;
        args.follow_links = 1; break;
      case 'e': args.pattern = optarg; break;
      case 1:   /* --color / --colour */
        if (!optarg)                        args.color = COLOR_ALWAYS;
        else if (std::string_view(optarg) == "always") args.color = COLOR_ALWAYS;
        else if (std::string_view(optarg) == "never")  args.color = COLOR_NEVER;
        else if (std::string_view(optarg) == "auto")   args.color = COLOR_AUTO;
        else
        {
          fprintf(stderr, "grep: invalid --color argument: %s\n", optarg);
          exit(1);
        }
            break;
      case '?': exit(1);
    }
  }

  // no -e
  if (!args.pattern)
  {
    if (optind >= argc)
    {
      fprintf(stderr, "grep: missing pattern\n");
      exit(1);
    }
    args.pattern = argv[optind++];
  }

  args.files = &argv[optind];
  args.file_count = argc - optind;

  if (args.recursive && args.file_count == 0)
  {
    static char* default_path[] = { (char*) "." };
    args.files = default_path;
    args.file_count = 1;
  }

  return args;
}
GrepArgs args;

enum PatternType
{
  CHAR = 0x0,
  DIGIT,
  W_CHAR,
  PLUS,
  STAR,
  N_QUANTIFIER,
  OPTIONAL,
  OR,
  CHAR_GROUP_POSITIVE,
  CHAR_GROUP_NEGATIVE,
  WILDCARD,
  GROUP,
  BACK_REF,
  NONE,
  COUNT
};

typedef struct Pattern
{
  PatternType type;
  PatternType quantifier;
  char c_char;
  char p_char;
  char n_char;
  std::vector<Pattern> group;
  std::string char_group;
  int cap_group;
  int min_quant;
  int max_quant;
} Pattern;
int cap_group_cnt = 0;
Pattern parse_single_pattern(std::string pattern, int& idx)
{
  Pattern result = {};
  result.type = CHAR;
  result.quantifier = NONE;
  result.c_char = pattern[idx];
  result.n_char = 0;
  result.p_char = 0;
  if (idx - 1 >= 0)
    result.p_char = pattern[idx - 1];
  if (idx + 1 < pattern.length())
    result.n_char = pattern[idx + 1];

  switch (result.c_char)
  {
    case '\\':
    {
      idx++;
      switch (pattern[idx])
      {
        case 'd': result.type = DIGIT; break;
        case 'w': result.type = W_CHAR; break;
        default:
          if (isdigit(pattern[idx]))
          {
            result.type = BACK_REF;
            result.c_char = pattern[idx] - '0';
          }
          break;
      }
      result.n_char = pattern[idx + 1];
      break;
    }
    case '[':
    {
      result.type = CHAR_GROUP_POSITIVE;
      idx++;
      if (pattern[idx] == '^')
      {
        result.type = CHAR_GROUP_NEGATIVE;
        idx++;
      }
      std::ostringstream ss;
      char prev = 0;
      while (pattern[idx] != ']')
      {
        if (pattern[idx] == '-' && prev != 0)
        {
          idx++;
          while (prev < pattern[idx])
          {
            ss << prev;
            prev++;
          }
        }
        prev = pattern[idx];
        ss << prev;
        idx++;
      }
      result.char_group = ss.str();
      result.n_char = pattern[idx + 1];
      break;
    }
    case '(':
    {
      result.type = GROUP;
      idx++;
      std::vector<Pattern> group;
      bool alt = false;
      result.cap_group = ++cap_group_cnt;

      while (pattern[idx] != ')')
      {
        auto pat = parse_single_pattern(pattern, idx);
        group.push_back(pat);
        if (pat.quantifier == OR)
        {
          alt = true;
          Pattern alternate = {};
          alternate.type = GROUP;
          alternate.quantifier = OR;
          alternate.group = group;
          alternate.cap_group = cap_group_cnt;
          result.group.push_back(alternate);
          group = {};
        }
      }
      if (alt)
      {
        Pattern alternate = {};
        alternate.type = GROUP;
        alternate.quantifier = OR;
        alternate.group = group;
        result.group.push_back(alternate);
      } else
      {
        for (auto e : group)
          result.group.push_back(e);
      }
      result.n_char = pattern[idx + 1];
      break;
    }
    case '.': result.type = WILDCARD; break;

    default: break;
  }

  switch (result.n_char)
  {
    case '+': result.quantifier = PLUS;    idx++; result.n_char = pattern[idx + 1];  break;
    case '*': result.quantifier = STAR;     idx++; result.n_char = pattern[idx + 1];  break;
    case '?': result.quantifier = OPTIONAL; idx++; result.n_char = pattern[idx + 1];  break;
    case '|': result.quantifier = OR;       idx++; result.n_char = pattern[idx + 1];  break;
    case '{':
    {
      result.quantifier = N_QUANTIFIER;
      idx += 2;
      result.min_quant = pattern[idx] - '0';
      result.max_quant = pattern[idx] - '0';
      idx++;
      if (pattern[idx] == ',')
      {
        idx++;
        if (pattern[idx] == '}')
          result.max_quant = INT_MAX;
        else
        {
          result.max_quant = pattern[idx] - '0';
          idx++;
        }
      }
      result.n_char = (idx + 1) < pattern.length() ? pattern[idx + 1] : 0;
    }
    default: break;
  }

  // moving past ')' for next_char
  int ni = idx + 1;
  while (ni < pattern.length() && result.n_char == ')')
    result.n_char = pattern[++ni];

  if (result.quantifier == PLUS || result.quantifier == STAR)
  {
    int i = 2;
    while (result.n_char == result.c_char && i + idx < pattern.length())
      result.n_char = pattern[idx + i++];
  }

  idx++;
  return result;
}

std::vector<Pattern> parse_whole_pattern(std::string pattern)
{
  std::vector<Pattern> res;
  for (int i = 0; i < pattern.length();)
  {
    res.push_back(parse_single_pattern(pattern, i));
  }
  return res;
}

std::vector<std::string> captures;

bool match_single(Pattern pattern, char chr)
{
  switch (pattern.type)
  {
    case WILDCARD:            return true;
    case CHAR:                return pattern.c_char == chr;
    case DIGIT:               return isdigit(chr);
    case W_CHAR:              return (isalnum(chr) || chr == '_');
    case CHAR_GROUP_POSITIVE: return pattern.char_group.contains(chr);
    case CHAR_GROUP_NEGATIVE: return !pattern.char_group.contains(chr);
    default:                  return false;
  }
  return false;
}
bool match_group(Pattern& pattern, const std::string& input, int& idx, std::vector<Pattern>& patterns);
bool n_quantifier(Pattern& pattern, const std::string& input, int& idx, std::vector<Pattern>& patterns, int pidx);

bool match_curr_pattern(Pattern& pattern, const std::string& input, int& idx, std::vector<Pattern>& patterns, int pidx)
{
  if (pattern.quantifier == N_QUANTIFIER)
    return n_quantifier(pattern, input, idx, patterns, pidx);

  if (pattern.type == GROUP)
    return match_group(pattern, input, idx, patterns);

  else if (pattern.type == BACK_REF)
  {
    int capture_group_idx = pattern.c_char;
    if (capture_group_idx > captures.size())
      return false;
    std::string group = captures[capture_group_idx];
    int saved_idx = idx;
    for (auto chr : group)
    {
      if (chr != input[idx++])
      {
        idx = saved_idx;
        return false;
      }
    }
    return true;
  }

  char chr = input[idx++];

  if (!match_single(pattern, chr))
  {
    if ((pattern.type == CHAR && (pattern.quantifier == STAR || pattern.quantifier == OPTIONAL)))
      idx--;
    else
      return false;
  }

  if (pattern.quantifier == STAR || pattern.quantifier == PLUS)
  {
    while (idx < input.length() && match_single(pattern, input[idx]) && pattern.n_char != input[idx])
      idx++;

    while (pattern.type != WILDCARD && ++pidx < patterns.size() && match_single(patterns[pidx], chr))
      idx--;
  }
  return true;
}

bool match_group(Pattern& pattern, const std::string& input, int& idx, std::vector<Pattern>& patterns)
{
  int saved_idx = idx;
  int pidx = 0;
  for (int i = 0; i < pattern.group.size(); ++i)
  {
    if (idx >= input.length())
      return false;
    auto p = pattern.group[i];
    if (!match_curr_pattern(p, input, idx, pattern.group, pidx))
    {
      idx = saved_idx;
      pidx = 0;
      if (p.quantifier != OR || (i + 1) >= pattern.group.size())
        return false;
    } else
    {
      if (p.quantifier == OR)
        break;
      pidx++;
    }
  }
  captures[pattern.cap_group] = input.substr(saved_idx, idx - saved_idx);
  return true;
}

bool n_quantifier(Pattern& pattern, const std::string& input, int& idx, std::vector<Pattern>& patterns, int pidx)
{
  int saved_idx = idx;
  Pattern dummy = pattern;
  dummy.quantifier = NONE;

  int matched_times = 0;
  while (idx < input.length() && matched_times < pattern.max_quant && pattern.n_char != input[idx])
  {
    if (match_curr_pattern(dummy, input, idx, pattern.group, pidx))
    {
      matched_times++;
    }
  }
  if (matched_times < pattern.min_quant)
  {
    idx = saved_idx;
    return false;
  }
  return true;
}

bool match_pattern(std::string& input, std::string pattern_text, std::ostringstream& out)
{
  bool anchor_beg = false;
  bool found_beg = false;
  if (pattern_text[0] == '^')
  {
    found_beg = true;
    anchor_beg = true;
    pattern_text = pattern_text.substr(1);
  }

  auto patterns = parse_whole_pattern(pattern_text);
  captures.assign(cap_group_cnt + 1, "");

  int pattern_len = patterns.size();
  int input_len = input.size();
  int i = 0, pi = 0;
  int last_found_idx = 0;
  for (; i < input_len && pi < pattern_len;)
  {
    int last_matched_idx = i;
    if (match_curr_pattern(patterns[pi], input, i, patterns, pi))
    {
      if (!found_beg)
        last_found_idx = i;
      found_beg = true;
      pi++;
      out << BEG << input.substr(last_matched_idx, i - last_matched_idx) << END;
      // if (args.only_match)
        // out << '\n';
    } else
    {
      pi = 0;
      i = ++last_found_idx;
      if (not args.only_match)
        out << input[i - 1];

      found_beg = false;
      if (anchor_beg)
      {
        out.str("");
        return false;
      }
    }
  }
  input = input.substr(i, input.size() - i);
  while (pi < pattern_len)
  {
    if (patterns[pi].c_char == '$' && i == input_len)
      return found_beg;
    if (!(patterns[pi].quantifier == STAR || patterns[pi].quantifier == OPTIONAL))
      return false;
    pi++;
  }
  return found_beg;
}

int main(int argc, char* argv[])
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  args = parse_args(argc, argv);

  if (args.color == COLOR_ALWAYS)
  {
    BEG = COL_MATCH;
    END = COL_RESET;
  }

  std::vector<std::string> paths;

  if (args.recursive)
  {
    for (auto& entry : std::filesystem::recursive_directory_iterator(args.files[0]))
    {
      if (entry.is_regular_file())
        paths.push_back(entry.path());
    }
  }
  for (int i = 0; i < args.file_count; ++i)
  {
  	 std::string path = args.files[i];
      paths.push_back(path);
  }
  
  if (not paths.empty())
  {
    int res = 1;
    for (auto filename : paths)
    {
      std::ifstream file(filename);
      if (not file.is_open())
        std::cerr << " Failed to open file " << '\n';

      std::string prefix = "";
      if (paths.size() > 1)
        prefix = filename + ':';

      std::string input_line;

      while (std::getline(file, input_line))
      {
        std::ostringstream out;
        if (match_pattern(input_line, args.pattern, out))
        {
          std::cout << prefix << input_line << '\n';
          res = 0;
        }
      }
    }
    return res;
  }

  int res = 1;
  std::string input_line;
  const bool anchored = args.pattern && args.pattern[0] == '^';
  while (std::getline(std::cin, input_line))
  {
    std::ostringstream out;
    std::string remaining = input_line;

    if (anchored)
    {
      if (match_pattern(remaining, args.pattern, out))
      {
        res = 0;
        std::cout << out.str();
        if (!args.only_match)
          std::cout << remaining << '\n';
      }
      continue;
    }
  
    bool did_find = false;
    for (bool matched = false; (matched = match_pattern(remaining, args.pattern, out)) || !remaining.empty(); )
    {
      if (matched)
      {
        did_find = true;
        res = 0;
      }
      std::cout << out.str();
      out.str("");
      if (args.only_match)
        std::cout << '\n';
    }

    if (did_find)
    {
      std::cout << out.str();
      if (!args.only_match)
        std::cout << '\n';
    }
  }
  return res;

}
