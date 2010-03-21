<?php
// $Id$

/**
 * @file
 *   Profiler install profile.
 *   Magic to make install profiles easy.
 */

/**
 * Implementation of hook_profile_details().
 */
function profiler_profile_details() {
  return array(
    'name' => 'Profiler',
    'description' => 'Magically Delicious.',
  );
}

/**
 * Implementation of hook_profile_modules().
 */
function profiler_profile_modules() {
  global $distribution;

  $modules = array_unique($distribution['modules']['core']);

  if (isset($distribution['modules-unset'])) {
    $modules = array_diff($modules, $distribution['modules-unset']);
  }

  return $modules;
}

/**
 * Implementation of hook_form_install_select_profile_form_alter() on behalf of System 
 * module.
 */
function system_form_install_select_profile_form_alter(&$form, $form_state) {
  profiler_load_api();

  // Hide this profile as a selection.
  $form['profile']['Profiler']['#access'] = FALSE;
  $submit = $form['submit'];
  unset($form['submit']);

  foreach (element_children($form['profile']) as $key) {
    $item = &$form['profile'][$key];
    $item['#value'] = NULL;
    $item['#parents'] = array('profiler');
    $existing[] = $item['#return_value'];
  }

  foreach (profiler_load_includes() as $name => $profile) {
    if (!in_array($name, $existing)) {
      $form['profiler'][$name] = array(
        '#type' => 'radio',
        '#return_value'=> $name,
        '#title' => $profile['name'],
        '#parents' => array('profiler'),
      );
    }
  }

  $form['submit'] = $submit;
  $form['#validate'][] = 'profiler_install_select_profile_form_validate';
}

/**
 * Submit handler for the install_select_profile_form. This is what handles 
 * redirecting to the appropriate install page.
 */
function profiler_install_select_profile_form_validate($form, &$form_state) {
  $profile = $form_state['values']['profiler'];
  if ($form_state['values']['profiler']) {
    if (isset($form['profiler'][$profile])) {
      install_goto("install.php?profile=profiler&profiler=$profile");
    }
    else {
      install_goto("install.php?profile=$profile");
    }
  }
  else {
    install_goto("install.php");
  }
}

/**
 * Loads all includes for Profiler install profile.
 */
function profiler_load_api() {
  static $included = FALSE;

  if (!$included) {
    $path = dirname(__FILE__);
    include_once($path .'/profiler.inc');
    $included = TRUE;
  }
}

/**
 * Returns an array list of contrib modules.
 */
function profiler_contrib_modules() {
  global $distribution;
  $modules = array();

  // We don't want to return any core modules.
  unset($distribution['modules']['core']);

  // Combine all modules just to be sure.
  foreach($distribution['modules'] as $array) {
    if (is_array($array)) {
      $modules = array_merge($modules, $array);
    }
  }

  if (!empty($distribution['modules-unset'])) {
    $modules = array_diff($modules, $distribution['modules-unset']);
  }

  return array_unique($modules);
}

/**
 * Returns an array list of features.
 */
function profiler_features() {
  global $distribution;
  $features = isset($distribution['features']) ? $distribution['features'] : array();

  if (!empty($distribution['features-unset']) && !empty($features)) {
    $features = array_diff($features, $distribution['features-unset']);
  }

  return array_unique($features);
}

/**
 * Implementation of hook_profile_task_list().
 */
function profiler_profile_task_list() {
  $tasks = array(
    'webgear-configure' => st('Configure WebGear'),
  );

  return $tasks;
}

/**
 * Implementation of hook_profile_tasks().
 */
function profiler_profile_tasks(&$task, $url, $theme_key = NULL) {  
  // Just in case some of the future tasks adds some output
  $output = '';

  // Install some more modules and maybe localization helpers too
  if ($task == 'profile') {
    $modules = profiler_contrib_modules();
    $files = module_rebuild_cache();
    $operations = array();
    foreach ($modules as $module) {
      $operations[] = array('_install_module_batch', array($module, $files[$module]->info['name']));
    }
    $batch = array(
      'operations' => $operations,
      'finished' => 'profiler_profile_batch_finished',
      'title' => st('Installing @drupal', array('@drupal' => drupal_install_profile_name())),
      'error_message' => st('The installation has encountered an error.'),
    );
    // Start a batch, switch to 'profile-install-batch' task. We need to
    // set the variable here, because batch_process() redirects.
    variable_set('install_task', 'profile-install-batch');
    batch_set($batch);
    batch_process($url, $url);
  }

  // Run additional configuration tasks
  // @todo Review all the cache/rebuild options at the end, some of them may not be needed
  // @todo Review for localization, the time zone cannot be set that way either
  if ($task == 'webgear-configure') {
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

    profiler_install($theme_key);

    // Save the features to a variable to be installed after first navigating to the site.
    variable_set('webgear_initial_features', profiler_features());

    // Get out of this batch and let the installer continue
    $task = 'profile-finished';
  }
  return $output;
}

/**
 * Finished callback for the modules install batch.
 *
 * Advance installer task to language import.
 */
function profiler_profile_batch_finished($success, $results) {
  variable_set('install_task', 'webgear-configure');
}

/**
 * Implementation of hook_form_alter().
 *
 * Allows the profile to alter the site-configuration form. This is
 * called through custom invocation, so $form_state is not populated.
 */
function profiler_form_alter(&$form, $form_state, $form_id) {
  if ($form_id == 'install_configure') {
    variable_set('site_name', 'Site Name');
    variable_set('site_mail', 'changeme@email.com');
  }
}

/**
 * Call all the functions to install basic.
 */
function profiler_install($theme_key = NULL, $distro_name = 'basic') {
  global $distribution;

  if ($theme_key && ($theme_key != 'default')) {
    $distribution['theme'] = $theme_key;
  }
  $distribution['theme'] = isset($distribution['theme']) ? $distribution['theme'] : 'garland';

  profiler_install_distribution();
  profiler_install_configure();
}

/**
 *
 */
function profiler_install_distribution() {
  global $distribution;

  variable_set('webgear_distribution', $distribution['distro_name']);

  if (is_array($distribution)) {
    require_once('./profiles/includes/distribution.inc');
    foreach($distribution as $name => $data) {
      $fnc = 'distribution_install_'. str_replace('-', '_', $name);
      if (function_exists($fnc)) {
        $fnc($data);
      }
    }
  }
}

/**
 *
 */
function profiler_load_distribution_file($distro_name, $info = array()) {
  $distribution = array();
  $file = "./profiles/distributions/$distro_name.info";

  if (is_file($file)) {
    if (function_exists('drupal_parse_info_file')) {
      $distribution = drupal_parse_info_file($file);
    }
    if (isset($distribution['base'])) {
      $distribution = profiler_load_distribution_file($distribution['base'], $distribution);
    }
    if (!empty($info)) {
      $distribution = profiler_distribution_union_recursive($distribution, $info);
    }
  }

  return $distribution;
}

/**
 * Helper function to union two arrays recursively
 */
function profiler_distribution_union_recursive($array1, $array2) {
  foreach ($array2 as $key => $value) {
    if (!isset($array1[$key])) {
      $array1[$key] = $value;
      continue;
    }
    $value = is_array($value) ? profiler_distribution_union_recursive($array1[$key], $value) : $value;

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
function profiler_install_configure() {
  global $user;

  // We need to mimic being user 1 in order to bypass administeruserbyrole.
  $user->uid = 1;

  // Turn this off temporarily so that we can pass a password through.
  variable_set('user_email_verification', FALSE);

  // We precreated user 1 with placeholder values. Let's save the real values.
  $account = user_load(1);

  $array = array(
    'name' => 'webgear_guru',
    'pass' => '5pr0ck3t_PW',
    'roles' => array(),
    'status' => 1,
    'mail' => 'fancymail@webgearapp.com',
  );

  user_save($account, $array);
  // Log in the first user.
  user_authenticate($array);
  variable_set('user_email_verification', TRUE);

  variable_set('clean_url', TRUE);
  // The user is now logged in, but has no session ID yet, which
  // would be required later in the request, so remember it.
  $user->sid = session_id();

  // Record when this install ran.
  variable_set('install_time', time());
}