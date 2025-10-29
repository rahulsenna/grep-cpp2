#include <iostream>
#include <string>
#include <vector>
#include <sstream>

enum PatternType
{

  CHAR = 0x0,
  DIGIT,
  W_CHAR,
  PLUS,
  STAR,
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
} Pattern;
int cap_group_cnt = 0;
Pattern parse_single_pattern(std::string pattern, int &idx)
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
      while(pattern[idx] != ']')
      {
        if (pattern[idx] == '-' && prev != 0)
        {
          idx++;
          while(prev < pattern[idx])
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
        for (auto e: group)
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
    case '+': result.quantifier = PLUS ;    idx++; result.n_char = pattern[idx+1];  break;
    case '*': result.quantifier = STAR;     idx++; result.n_char = pattern[idx+1];  break;
    case '?': result.quantifier = OPTIONAL; idx++; result.n_char = pattern[idx+1];  break;
    case '|': result.quantifier = OR;       idx++; result.n_char = pattern[idx+1];  break;
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
  switch(pattern.type)
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
bool match_group(Pattern pattern, const std::string &input, int &idx, std::vector<Pattern> &patterns);

bool match_curr_pattern(Pattern pattern, const std::string &input, int &idx, std::vector<Pattern> &patterns, int pidx)
{
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

    while (++pidx < patterns.size() && match_single(patterns[pidx], chr))
      idx--;
  }
  return true;
}

bool match_group(Pattern pattern, const std::string &input, int &idx, std::vector<Pattern> &patterns)
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
      if (p.quantifier != OR || (i+1) >= pattern.group.size())
        return false;
    }
    else
    {
      if (p.quantifier == OR)
        break;
      pidx++;
    }
  }
  captures[pattern.cap_group] = input.substr(saved_idx, idx - saved_idx);
  return true;
}

bool match_pattern(const std::string &input, std::string pattern_text)
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
    if (match_curr_pattern(patterns[pi], input, i, patterns, pi))
    {
       if (!found_beg)
        last_found_idx = i;
      found_beg = true;
      pi++;
    }
    else
    {
      pi = 0;
      i = ++last_found_idx;

      found_beg = false;
      if (anchor_beg)
        return false;
    }
  }
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

int main(int argc, char *argv[])
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cerr << "Logs from your program will appear here" << std::endl;

  if (argc != 3)
  {
    std::cerr << "Expected two arguments" << std::endl;
    return 1;
  }

  std::string flag = argv[1];
  std::string pattern = argv[2];

  if (flag != "-E")
  {
    std::cerr << "Expected first argument to be '-E'" << std::endl;
    return 1;
  }


#if 1 // DEBUG
  std::string input_line;
  std::getline(std::cin, input_line);
#else
  std::string input_line = "abc-def is abc-def, not efg, abc, or def";
  pattern = "(([abc]+)-([def]+)) is \\1, not ([^xyz]+), \\2, or \\3";
#endif
  try
  {
    if (match_pattern(input_line, pattern))
    {
      return 0;
    }
    else
    {
      return 1;
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
