<?php
// $Id$

/**
 * @file
 *   Holds API and helper functions for Profiler install profile.
 */

/**
 * Finds all Profiler install profile includes.
 *
 * @return
 *   An array of file names
 */
function profiler_find_includes() {
  $path = dirname(__FILE__);
  return file_scan_directory($path .'/profiles', '\.inc$', array('.', '..', 'CVS'));
}

/**
 * Load the profile include.
 */
function profiler_load_includes() {
  $profiles = array();

  foreach(profiler_find_includes() as $filename => $data) {
    $profiles[$data->name] = drupal_parse_info_file($filename);
  }

  return $profiles;
}

/**
 * Loads up a Profiler profile array.
 */
function profiler_load_include($name, $sub_profile = array()) {
  static $profiles = array();

  $path = dirname(__FILE__) .'/profiles';
  $file = "$path/$name.inc";

  if (is_file($file)) {
    // drupal_parse_info_file() may not be available, depending on the bootstrap phase.
    if (function_exists('drupal_parse_info_file') && !isset($profiles[$name])) {
      $profiles[$name] = drupal_parse_info_file($file);
    }
    else {
      $profile = $profiles[$name];
    }
    if (isset($profile['base'])) {
      $profile = profiler_load_include($profile['base'], $profile);
    }
    if (!empty($sub_profile)) {
      $profile = profiler_profile_union_recursive($profile, $sub_profile);
    }
  }

  return $profile;
}

/**
 * Helper function to union two arrays recursively.
 */
function profiler_profile_union_recursive($array1, $array2) {
  foreach ($array2 as $key => $value) {
    if (!isset($array1[$key])) {
      $array1[$key] = $value;
      continue;
    }
    $value = is_array($value) ? profiler_profile_union_recursive($array1[$key], $value) : $value;

    if (is_numeric($key)) {
      $array1[] = $value;
    }
    else {
      $array1[$key] = $value;
    }
  }
  return $array1;
}