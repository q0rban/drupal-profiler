<?php
// $Id$

/**
 * @see hook_profile_task_list().
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_profile_task_list($profile_name) {
  $tasks = array();

  $tasks['profiler-modules'] = st('Install Modules');

  if ($features = profiler_features($profile_name)) {
    $tasks['profiler-features'] = st('Install Features');
  }

  $tasks['profiler-install'] = st('Additional Configuration');

  return $tasks;
}

/**
 * @see hook_profile_tasks().
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_profile_tasks(&$task, $url, $profile_name) {
  // Just in case some of the future tasks adds some output
  $output = '';

  if ($task == 'profile') {
    // Move along, nothing to do here.
    $task = 'profile-modules';
  }

  // Install some contrib modules.
  if ($task == 'profile-modules') {
    $modules = profiler_contrib_modules($profile_name);

    // Ensure the features module is here if there are features in the config.
    if (!in_array('features', $modules) && profiler_features($profile_name)) {
      $modules[] = 'features';
    }

    $files = module_rebuild_cache();
    $operations = array();
    foreach ($modules as $module) {
      $operations[] = array('_install_module_batch', array($module, $files[$module]->info['name']));
    }
    $batch = array(
      'operations' => $operations,
      'finished' => 'profiler_modules_batch_finished',
      'title' => st('Installing @drupal', array('@drupal' => drupal_install_profile_name())),
      'error_message' => st('The installation has encountered an error.'),
    );
    // Start a batch, switch to 'profile-install-batch' task. We need to
    // set the variable here, because batch_process() redirects.
    variable_set('install_task', 'profile-install-batch');
    batch_set($batch);
    batch_process($url, $url);
  }

  // Install all features.
  if ($task == 'profiler-features') {
    if ($features = profiler_features($profile_name)) {
      $files = module_rebuild_cache();
      $operations = array();
      foreach ($features as $feature) {
        $operations[] = array('_install_module_batch', array($feature, $files[$feature]->info['name']));
      }
      $batch = array(
        'operations' => $operations,
        'finished' => 'profiler_features_batch_finished',
        'title' => st('Installing @drupal', array('@drupal' => drupal_install_profile_name())),
        'error_message' => st('The installation has encountered an error.'),
      );
      // Start a batch, switch to 'profile-install-batch' task. We need to
      // set the variable here, because batch_process() redirects.
      variable_set('install_task', 'profile-install-batch');
      batch_set($batch);
      batch_process($url, $url);
    }
    else {
      profiler_features_batch_finished(TRUE, array());
      $task = 'profiler-install';
    }
  }

  // Installs the actual profile.
  // @todo Review all the cache/rebuild options at the end, some of them may not be needed
  if ($task == 'profiler-install') {
    $config = profiler_load_config($profile_name);
    // Remove default input filter formats
    $result = db_query("SELECT * FROM {filter_formats} WHERE name IN ('%s', '%s')", 'Filtered HTML', 'Full HTML');
    while ($row = db_fetch_object($result)) {
      db_query("DELETE FROM {filter_formats} WHERE format = %d", $row->format);
      db_query("DELETE FROM {filters} WHERE format = %d", $row->format);
    }

    // Set time zone
    variable_set('date_default_timezone_name', 'US/Eastern');

    // Calculate time zone offset from time zone name and set the default timezone offset accordingly.
    // You dont need to change the next two lines if you change the default time zone above.
    $date = date_make_date('now', variable_get('date_default_timezone_name', 'US/Eastern'));
    variable_set('date_default_timezone', date_offset_get($date));

    // Rebuild key tables/caches
    menu_rebuild();
    module_rebuild_cache(); // Detects the newly added bootstrap modules
    node_access_rebuild();
    drupal_get_schema('system', TRUE); // Clear schema DB cache
    drupal_flush_all_caches();
    db_query("UPDATE {blocks} SET status = 0, region = ''"); // disable all DB blocks

    profiler_install($profile_name);

    // Get out of this batch and let the installer continue
    $task = 'profile-finished';
  }

  return $output;
}

/**
 * @see hook_profile_modules().
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_profile_modules($profile_name) {
  $config = profiler_load_config($profile_name);

  $modules = array_unique($config['modules']['core']);

  if (!empty($config['modules-unset'])) {
    $modules = array_diff($modules, $config['modules-unset']);
  }

  // We can't do anything without the Install Profile API module!
  $modules[] = 'install_profile_api';

  return $modules;
}

/**
 * Returns an array list of contrib modules.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_contrib_modules($profile_name) {
  $config = profiler_load_config($profile_name);

  $modules = array();

  // We don't want to return any core modules here.
  unset($config['modules']['core']);

  // Combine all modules just to be sure.
  foreach($config['modules'] as $array) {
    if (is_array($array)) {
      $modules = array_merge($modules, $array);
    }
  }

  // Check to see if any modules have been unset by a Sub-Profile.
  if (!empty($config['modules-unset'])) {
    $modules = array_diff($modules, $config['modules-unset']);
  }

  return array_unique($modules);
}

/**
 * Returns an array list of features.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_features($profile_name) {
  $config = profiler_load_config($profile_name);

  $features = isset($config['features']) ? $config['features'] : array();

  if (!empty($config['features-unset']) && !empty($features)) {
    $features = array_diff($features, $config['features-unset']);
  }

  return array_unique($features);
}

/**
 * Finished callback for the modules install batch.
 *
 * Advance installer task to features install.
 */
function profiler_modules_batch_finished($success, $results) {
  variable_set('install_task', 'profiler-features');
}

/**
 * Finished callback for the features install batch.
 *
 * Advance installer task to localization stuff.
 */
function profiler_features_batch_finished($success, $results) {
  variable_set('install_task', 'profiler-install');
}

/**
 * Helper to implementation of hook_form_alter() for Install profiles.
 *
 * Allows the profile to alter the site-configuration form. This is
 * called through custom invocation, so $form_state is not populated.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_form_alter($profile_name, &$form, $form_state, $form_id) {
  $config = profiler_load_config($profile_name);

  // If site_name and site_mail are in the config, we can possibly bypass a step
  // during the installation.
  if ($form_id == 'install_configure' && 
    isset($config['variables']['site_name']) && 
    isset($config['variables']['site_mail']) && 
    profiler_config_load_superuser($config)
  ) {
    variable_set('site_name', $config['variables']['site_name']);
    variable_set('site_mail', $config['variables']['site_mail']);
  }
}

/**
 * Returns user 1 for a config, if it exists.
 */
function profiler_config_load_superuser($config) {
  if (isset($config['users'])) {
    foreach ($config['users'] as $account) {
      if ($account['uid'] == 1) {
        return $account;
      }
    }
  }
  return FALSE;
}

/**
 * Call all the functions to install basic.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 */
function profiler_install($profile_name) {
  $config = profiler_load_config($profile_name);

  profiler_install_config($config);
  profiler_install_configure($config);
}

/**
 * Calls each config installation function.
 */
function profiler_install_config($config) {
  if (is_array($config)) {
    require_once('./profiles/profiler/profiler_install.inc');
    foreach($config as $name => $data) {
      $fnc = 'profiler_install_'. str_replace('-', '_', $name);
      if (function_exists($fnc)) {
        $fnc($data, $config);
      }
    }
  }
}

/**
 * Load the config for a particular Install profile.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 * @param $reset
 *   Boolean to reset the static cache of the config.
 * @return
 *   The Install profiles Profiler include configuration, as an array.
 */
function profiler_load_config($profile_name, $reset = FALSE) {
  static $config;

  if (is_null($config) || $reset) {
    $config = profiler_load_config_file($profile_name);
  
    $config['profile_name'] = $profile_name;
  }

  return $config;
}

/**
 * Loads a Profiler include file, recursively unioning with base profiler 
 * includes if needed.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 * @param $info
 *   Internal use only, the Sub-Profile config array to be unioned with a 
 *   base Profiler include.
 */
function profiler_load_config_file($profile_name, $info = array()) {
  $config = array();

  if (($file = profiler_profile_config_file($profile_name)) && is_file($file)) {
    // Sometimes drupal_parse_info_file() isn't available, so we double check before
    // calling it to avoid any errors.
    if (function_exists('drupal_parse_info_file')) {
      $config = drupal_parse_info_file($file);
    }
    if (isset($config['base'])) {
      $config = profiler_load_config_file($config['base'], $config);
    }
    if (!empty($info)) {
      $config = profiler_config_union_recursive($config, $info);
    }
  }

  return $config;
}

/**
 * Returns the path to the Profiler include for an Install profile.
 *
 * @param $profile_name
 *   The machine name of the Install profile.
 * @return
 *   The string path to the Profiler include.
 */
function profiler_profile_config_file($profile_name) {
  // Check to see if this profile implements hook_profiler_info(). 
  // hook_profiler_info() can return an array with 'path' set to the path of the 
  // file that should be used as the Profiler include.
  $fnc = $profile_name .'_profile_details';
  $details = $fnc();

  return isset($details['path']) ? $details['path'] ."/$profile_name.profiler.inc" : FALSE;
}

/**
 * Helper function to union two arrays recursively.
 */
function profiler_config_union_recursive($array1, $array2) {
  foreach ($array2 as $key => $value) {
    if (!isset($array1[$key])) {
      $array1[$key] = $value;
      continue;
    }
    $value = is_array($value) ? profiler_config_union_recursive($array1[$key], $value) : $value;

    if (is_numeric($key)) {
      $array1[] = $value;
    }
    else {
      $array1[$key] = $value;
    }
  }
  return $array1;
}

/**
 * This function mimics a lot of the functionality of install_configure_form_submit() inside install.php
 */
function profiler_install_configure($config) {
  if ($array = profiler_config_load_superuser($config)) {
    global $user;

    // We need to mimic being user 1 in order to bypass administeruserbyrole.
    $user->uid = 1;

    // Load up the current user 1.
    $account = user_load(1);

    // The password is md5, so we need to unset it and save it manually.
    $pass = $array['pass'];
    unset($array['pass']);

    user_save($account, $array);
    // Log in the first user.
    user_authenticate($array);

    db_query("UPDATE {users} SET pass = '%s' WHERE uid = %d", $pass, 1);

    variable_set('clean_url', TRUE);
    // The user is now logged in, but has no session ID yet, which
    // would be required later in the request, so remember it.
    $user->sid = session_id();

    // Record when this install ran.
    variable_set('install_time', time());
  }
}