
#include "config_file.h"

#include <toml++/toml.h>

#include <shlobj_core.h>

#include <vector>
#include <fstream>
#include <cstring>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif


static const char* config_fn = "rtl_sdr_extio.cfg";
static char confFile[MAX_PATH + MAX_PATH];

// keys (strings) in .cfg file
static const std::string key_enable("enable");
static const std::string key_log("log");

static const std::string key_band_name("name");
static const std::string key_freq_from("freq_from");
static const std::string key_freq_to("freq_to");
static const std::string key_sampling_mode("sampling_mode");
static const std::string key_samplerate("samplerate");
static const std::string key_tuner_bandwidth("tuner_bandwidth");
static const std::string key_r820t_tuner_band_center("r820t_tuner_band_center");
static const std::string key_tuning_sideband("tuning_sideband");
static const std::string key_tuner_rf_agc("tuner_rf_agc");
static const std::string key_tuner_rf_gain_db("tuner_rf_gain_db");
static const std::string key_tuner_if_agc("tuner_if_agc");
static const std::string key_tuner_if_gain_db("tuner_if_gain_db");
static const std::string key_rtl_digital_agc("rtl_digital_agc");
static const std::string key_bias_tee("bias_tee");
static const std::string key_gpio_button0("gpio_button0");
static const std::string key_gpio_button1("gpio_button1");
static const std::string key_gpio_button2("gpio_button2");
static const std::string key_gpio_button3("gpio_button3");
static const std::string key_gpio_button4("gpio_button4");



static std::vector<BandAction> band_actions;

static BandAction::Band_Info band_status = BandAction::info_not_loaded;

static const BandAction initial_band_action{ "_init_", {}, -1.0, -1.0 };

static const BandAction* current_band_action = &initial_band_action;


static bool is_expected_value_type(
  const std::string& id, const std::string& key, const std::string& expected_key,
  const toml::node& val, std::ofstream& info_out)
{
  if (!key.compare(expected_key))
  {
    if (!val.is_number())
    {
      info_out << "error: '" << key << "' for band '" << id << "' is no value!\n";
      return false;
    }
    return true;
  }
  return false;
}


static bool is_expected_bool_type(
  const std::string& id, const std::string& key, const std::string& expected_key,
  const toml::node& val, std::ofstream& info_out)
{
  if (!key.compare(expected_key))
  {
    if (!val.is_boolean())
    {
      info_out << "error: '" << key << "' for band '" << id << "' is no bool!\n";
      return false;
    }
    return true;
  }
  return false;
}


static void parse_band_action(const std::string& id, const toml::table& tbl, std::ofstream& info_out)
{
  BandAction ba;
  ba.id = id;

  {
    auto it_freq_from = tbl.find(key_freq_from);
    if (it_freq_from == tbl.cend())
    {
      info_out << "error: no 'freq_from' for band '" << id << "'\n";
      return;
    }
    if (!it_freq_from->second.is_floating_point())
    {
      info_out << "error: 'freq_from' value for band '" << id << "' is not floating point!\n";
      return;
    }

    auto it_freq_to = tbl.find(key_freq_to);
    if (it_freq_to == tbl.cend())
    {
      info_out << "error: no 'freq_to' for band '" << id << "'\n";
      return;
    }
    if (!it_freq_to->second.is_floating_point())
    {
      info_out << "error: 'freq_to' value for band '" << id << "' is not floating point!\n";
      return;
    }

    ba.freq_from = it_freq_from->second.as_floating_point()->get();
    ba.freq_to = it_freq_to->second.as_floating_point()->get();
    if (ba.freq_from > ba.freq_to)
    {
      std::swap(ba.freq_from, ba.freq_to);
      info_out << "warning: freq_from = " << ba.freq_from
        << " is greater than freq_to = " << ba.freq_to
        << "for band '" << id << "'. swapped.\n";
    }
  }

  for (auto const& [key, val] : tbl)
  {
    if (!key.compare(key_freq_from) || !key.compare(key_freq_to))
      continue;

    else if (!key.compare(key_band_name))
    {
      if (!val.is_string())
      {
        info_out << "error: '" << key << "' for band '" << id
          << "' is no string!\n";
        continue;
      }
      const std::string val_str = val.as_string()->get();
      ba.name = val_str;
    }

    else if (!key.compare(key_sampling_mode))
    {
      if (!val.is_string())
      {
        info_out << "error: '" << key << "' for band '" << id
          << "' is no string! expected 'I', 'Q' or 'C'.\n";
        continue;
      }
      const std::string val_str = val.as_string()->get();
      if (!val_str.compare("I") || !val_str.compare("Q") || !val_str.compare("C"))
      {
        ba.sampling_mode = val_str[0];
      }
      else
      {
        info_out << "error: expected 'I', 'Q' or 'C' for key '" << key << "' for band '" << id << "!\n";
        continue;
      }
    }

    else if (!key.compare(key_tuning_sideband))
    {
      if (!val.is_string())
      {
        info_out << "error: '" << key << "' for band '" << id
          << "' is no string! expected 'L' or 'U'.\n";
        continue;
      }
      const std::string val_str = val.as_string()->get();
      if (!val_str.compare("L") || !val_str.compare("U"))
      {
        ba.tuning_sideband = val_str[0];
      }
      else
      {
        info_out << "error: expected 'L' or 'U' for key '" << key << "' for band '" << id << "!\n";
        continue;
      }
    }

    else if (is_expected_value_type(id, key, key_samplerate, val, info_out))
      ba.samplerate = val.as_floating_point()->get();

    else if (is_expected_value_type(id, key, key_tuner_bandwidth, val, info_out))
      ba.tuner_bandwidth = val.as_floating_point()->get();

    else if (is_expected_value_type(id, key, key_r820t_tuner_band_center, val, info_out))
      ba.r820t_tuner_band_center = val.as_floating_point()->get();

    else if (is_expected_value_type(id, key, key_tuner_rf_gain_db, val, info_out))
      ba.tuner_rf_gain_db = val.as_floating_point()->get();

    else if (is_expected_value_type(id, key, key_tuner_if_gain_db, val, info_out))
      ba.tuner_if_gain_db = val.as_floating_point()->get();

    else if (is_expected_bool_type(id, key, key_tuner_rf_agc, val, info_out))
      ba.tuner_rf_agc = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_tuner_if_agc, val, info_out))
      ba.tuner_if_agc = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_rtl_digital_agc, val, info_out))
      ba.rtl_digital_agc = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_bias_tee, val, info_out))
      ba.gpio_button0 = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_gpio_button0, val, info_out))
      ba.gpio_button0 = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_gpio_button1, val, info_out))
      ba.gpio_button1 = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_gpio_button2, val, info_out))
      ba.gpio_button2 = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_gpio_button3, val, info_out))
      ba.gpio_button3 = val.as_boolean()->get();

    else if (is_expected_bool_type(id, key, key_gpio_button4, val, info_out))
      ba.gpio_button4 = val.as_boolean()->get();

    else
    {
      info_out << "warning: '" << key << "' for band '" << id << "' is unknown.\n";
    }
  }

  info_out << "info: adding band '" << id << "'\n";
  band_actions.push_back(ba);
}


static void print_toml_tables(int level, toml::table& tbl, std::ofstream& info_out,
  const bool is_band = false
)
{
  //info_out << "print_toml_tables(level " << level << ")\n";
  for (auto const& [key, val] : tbl)
  {
    const bool is_comment = (key.length() && key[0] == '#');
    info_out << "level " << level << "  key: '" << key << "'";

    if (is_comment)
      info_out << "\n";
    else if (val.is_table())
    {
      info_out << "\n";
      print_toml_tables(level + 1, *(val.as_table()), info_out, !key.compare("bands"));
      if (is_band)
        parse_band_action(key, *(val.as_table()), info_out);
    }
    else if (val.is_boolean())
    {
      info_out << "  bool: " << val.as_boolean()->get() << "\n";
    }
    else if (val.is_floating_point())
    {
      info_out << "  float: " << val.as_floating_point()->get() << "\n";
    }
    else if (val.is_string())
    {
      const std::string& s = val.as_string()->get();
      info_out << "  string: " << s << "\n";
    }
    else
    {
      info_out << "  some other type\n";
    }
  }
}


static bool write_default_config(const char* fn)
{
  auto tbl = toml::table{ {
      { "# comment1", "info: keys starting with a '#' are interpreted as comments and ignored" },
      { "# comment2", "this file is automatically created as example/template, showing possible keys" },
      { "# enable", "thus, set enable to 'true' to activate; keep 'false' to deactivate" },
      { "# log", "log parsing to the file 'parsed_infos.txt'" },
      { key_enable, false },        // have it deactivated by default!
      { key_log, false },
      { "bands", toml::table{{
          { "# name", "optional: band name to display" },
          { "# freq_from", "mandatory: low band edge: frequency in Hz" },
          { "# freq_to", "mandatory: high band edge: frequency in Hz" },
          { "# sampling_mode", "optional: 'I', 'Q' or 'C' for complex/both lines" },
          { "# samplerate", "optional: samplerate" },
          { "# tuner_bandwidth", "optional" },
          { "# r820t_tuner_band_center", "optional" },
          { "# tuning_sideband", "optional: tuning_sideband" },     // @todo
          { "# tuner_rf_agc", "optional: " },
          { "# tuner_rf_gain_db", "optional" },
          { "# tuner_if_agc", "optional" },
          { "# tuner_if_gain_db", "optional" },
          { "# rtl_digital_agc", "optional" },
          { "# bias_tee", "optional: alias for 'gpio_button0'" },
          { "# gpio_button0", "optional: equals bias_tee" },
          { "# gpio_button1", "optional: the button state - NOT the GPIO state!" },
          { "# gpio_button2", "optional" },
          { "# gpio_button3", "optional" },
          { "# gpio_button4", "optional" },

          { "1", toml::table{{
              { key_band_name, "0..13 MHz (HF-DS)" },
              { key_freq_from, 0.0 },
              { key_freq_to, 13.0e6 },
              { key_sampling_mode, "Q" },
              { key_bias_tee, false },
              { key_tuner_rf_agc, true },
              { key_tuner_if_agc, true },
          }} },
          { "2", toml::table{{
              { key_band_name, "13..24.5 MHz (HF-DS)" },
              { key_freq_from, 13.0e6 },
              { key_freq_to, 24.5e6 },
              { key_sampling_mode, "Q" },
              { key_bias_tee, true },
              { key_tuner_rf_agc, true },
              { key_tuner_if_agc, true },
          }} },
          { "3", toml::table{{
              { key_band_name, "24.5..108 MHz" },
              { key_freq_from, 24.5e6 },
              { key_freq_to, 108.0e6 },
              { key_sampling_mode, "C" },
              { key_bias_tee, true },
              { key_tuner_rf_gain_db, 16.6 },
              { key_tuner_if_gain_db, 11.2 },
          }} },
          { "4", toml::table{{
              { key_band_name, "108..300 MHz" },
              { key_freq_from, 108.0e6 },
              { key_freq_to, 300.0e6 },
              { key_sampling_mode, "C" },
              { key_bias_tee, true },
              { key_tuner_rf_gain_db, 20.7 },
              { key_tuner_if_gain_db, 11.2 },
          }} },
          { "5", toml::table{{
              { key_band_name, "300..2000 MHz" },
              { key_freq_from, 300.0e6 },
              { key_freq_to, 2000.0e6 },
              { key_sampling_mode, "C" },
              { key_bias_tee, true },
              { key_tuner_rf_gain_db, 32.8 },
              { key_tuner_if_gain_db, 11.2 },
          }} },
      }} },
  } };

  std::ofstream config_file;
  config_file.open(fn);
  if (config_file.is_open())
  {
    config_file << tbl << "\n";
    config_file.close();
    return true;
  }
  return false;
}


const char* init_toml_config()
{
  // process only once
  static bool processed = false;
  if (processed)
    return confFile;
  processed = true;

  strncpy(confFile, config_fn, MAX_PATH + MAX_PATH - 1);
  confFile[MAX_PATH + MAX_PATH - 1] = 0;
  const char* fn = confFile;
  // prepend GetUserProfileDirectoryA() to config_fn ?
  if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, confFile)))
  {
    strncat(confFile, "\\", MAX_PATH + MAX_PATH - 1);
    strncat(confFile, config_fn, MAX_PATH + MAX_PATH -1);
    confFile[MAX_PATH + MAX_PATH - 1] = 0;
    fn = confFile;
  }

  FILE* f = fopen(fn, "r");
  if (!f)
  {
    // no config file => write one
    bool write_ok = write_default_config(fn);
    //if (!write_ok)
    //    ;
  }

  std::ofstream parsed_infos;

  toml::table tbl;
  bool parse_cfg = false;
  try
  {
    tbl = toml::parse_file(fn);
    auto it_log = tbl.find(key_log);
    if (it_log != tbl.end() && it_log->second.is_boolean())
      if (it_log->second.as_boolean()->get())
        parsed_infos.open("parsed_infos.txt");

    auto it_enable = tbl.find(key_enable);
    if (it_enable != tbl.end() && it_enable->second.is_boolean())
      parse_cfg = it_enable->second.as_boolean()->get();

    if (parse_cfg)
    {
      print_toml_tables(0, tbl, parsed_infos);

      if (!band_actions.size())
        band_status = BandAction::Band_Info::info_no_bands;
      else
        band_status = BandAction::Band_Info::info_ok;
    }
    else
      band_status = BandAction::Band_Info::info_disabled;
  }
  catch (const toml::parse_error& err)
  {
    band_status = BandAction::Band_Info::info_parse_error;
    parsed_infos << "Parsing failed : \n" << err << "\n";
  }

  parsed_infos.close();
  return fn;
}

BandAction::Band_Info get_band_info()
{
  return band_status;
}


const BandAction* update_band_action(double new_frequency)
{
  if (!band_actions.size())
    return nullptr;

  if (current_band_action
    && current_band_action->freq_from <= new_frequency
    && new_frequency <= current_band_action->freq_to
    )
  {
    // still in last band
    return nullptr;
  }

  // else: moved out of last band => no action
  current_band_action = nullptr;

  for (const auto& band : band_actions)
  {
    if (band.freq_from <= new_frequency && new_frequency <= band.freq_to)
    {
      // moved into new band => action
      current_band_action = &band;
      return current_band_action;
    }
  }

  // no new band
  return nullptr;
}

