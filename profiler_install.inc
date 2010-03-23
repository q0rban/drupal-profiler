<?php
// $Id$

/**
 * @file
 * The dirty work of Profiler!
 */

function profiler_install_roles($roles) {
  foreach($roles as $value) {
    if (is_array($value) && $value['rid'] > 2) {
      db_query("DELETE FROM {role} WHERE rid = %d", $value['rid']);
      db_query("INSERT INTO {role} (rid, name) VALUES (%d, '%s')", $value['rid'], $value['name']);
    }
    else {
      $value = is_array($value) ? $value['name'] : $value;
      db_query("INSERT INTO {role} (name) VALUES ('%s')", $value);
    }
  }
}

function profiler_install_users($users) {
  foreach($users as $array) {
    if ($array) {
      if ($array['uid'] && ($account = user_load($array['uid']))) {
        user_save($account, $array);
      }
      else {
        // For some reason db_last_insert_id() doesn't get the uid of the newly created 
        // user, so we need to separate out roles first, create the account, then save 
        // again to properly assign roles to this user.
        $roles = $array['roles'];

        // Passwords should be md5 hashed values, so we need to separate the pass field 
        // out first.
        $pass = $array['pass'];

        unset($array['roles'], $array['pass']);
        user_save(new stdClass(), $array);

        $account = user_load(array('name' => $array['name']));
        _profiler_install_users_roles($account, $roles);
        db_query("UPDATE {users} SET pass = '%s' WHERE name = '%s'", $pass, $array['name']);
      }
    }
  }
}

function _profiler_install_users_roles($account, $roles) {
  $formatted = array();
  $roles = explode(',', $roles);

  foreach ($roles as $name) {
    if ($rid = db_result(db_query("SELECT rid FROM {role} WHERE name = '%s'", trim($name)))) {
      $formatted[$rid] = TRUE;
    }
  }

  user_save($account, array('roles' => $formatted));
}

function profiler_install_input_formats($formats) {
  install_include(array('filter'));

  foreach($formats as $name => $data) {
    if ($data) {
      $format_id = install_add_format($name);
  
      if ($data['default']) {
        variable_set('filter_default_format', $format_id);
      }
  
      if ($data['allowed-html']) {
        variable_set('allowed_html_'. $format_id, $data['allowed-html']);
      }
  
      if ($data['roles']) {
        $roles = array();
        foreach (explode(',', $data['roles']) as $name) {
          if ($rid = db_result(db_query("SELECT rid FROM {role} WHERE name = '%s'", trim($name)))) {
            $roles[] = $rid;
          }
        }
        if (!empty($roles)) {
          install_format_set_roles($roles, $format_id);
        }
      }
  
      if ($data['filters']) {
        db_query("DELETE FROM {filters} WHERE format = %d", $format_id);
        foreach($data['filters'] as $module => $deltas) {
          if (is_array($deltas)) {
            foreach($deltas as $delta => $weight) {
              db_query("INSERT INTO {filters} (format, module, delta, weight) VALUES (%d, '%s', %d, %d)", $format_id, $module, $delta, $weight);
            }
          }
        }
      }
  
      if ($data['wysiwyg'] && module_exists('wysiwyg')) {
        $editor = $data['wysiwyg']['editor'];
        $settings = serialize($data['wysiwyg']['settings']);
  
        // Insert new profile data.
        db_query("UPDATE {wysiwyg} SET settings = '%s', editor = '%s' WHERE format = %d", $settings, $editor, $format_id);
        if (!db_affected_rows()) {
          db_query("INSERT INTO {wysiwyg} (format, editor, settings) VALUES (%d, '%s', '%s')", $format_id, $editor, $settings);
        }
      }
    }
  }
}

function profiler_install_variables($vars) {
  foreach($vars as $key => $value) {
    variable_set($key, $value);
  }
}

function profiler_install_permissions($permissions) {
  install_include(array('user'));
  $perms = array();

  foreach ($permissions as $perm => $roles) {
    $roles = explode(',', $roles);
    foreach ($roles as $role) {
      $perms[$role][] = $perm;
    }
  }

  foreach($perms as $name => $permissions) {
    if ($rid = install_get_rid($name)) {
      install_add_permissions($rid, $permissions);
    }
  }
}

function profiler_install_node_types($types) {
  install_include(array('node'));
  foreach($types as $properties) {
    if ($properties) {
      $content_type = $properties['type'];
      $content_name = $properties['name'];
      install_create_content_type($content_type, $content_name, $properties);
    }
  }
}

function profiler_install_nodes($nodes) {
  install_include(array('node'));
  foreach($nodes as $nid => $properties) {
    if ($properties) {
      if (is_numeric($nid)) {
        $properties['nid'] = $nid;
      }

      if ($properties['format']) {
        _profiler_format_set_fid($properties['format']);
      }

      $title = $properties['title'];
      $body = $properties['body'];

      install_create_node($title, $body, $properties);
    }
  }
}

function profiler_install_menus($menus) {
  install_include(array('menu'));
  foreach($menus as $menu) {
    if ($menu) {
      install_menu_create_menu($menu['title'], $menu['name'], $menu['description']);
    }
  }
}

function profiler_install_menu_items($menu_items) {
  menu_rebuild();
  install_include(array('menu'));

  $installed = array();
  foreach($menu_items as $item) {
    // We need a link_path to do anything.
    if ($item['link_path']) {
      // We want to make sure this one hasn't already been installed.
      if (!in_array($item['link_path'], $installed) && $items = install_menu_get_items($item['link_path'])) {
        foreach($items as $menu_item) {
          if ($menu_item['mlid']) {
            $item['mlid'] = $menu_item['mlid'];
            menu_link_save($item);
          }
        }
      }
      elseif($item['link_title']) {
        install_menu_create_menu_item(
          $item['link_path'],
          $item['link_title'],
          $item['description'],
          $item['menu_name'],
          $item['plid'],
          $item['weight'],
          $item['module'],
          $item['hidden'],
          $item['has_children'],
          $item['expanded'],
          $item['customized'],
          $item['updated']
        );
      }
      $installed[] = $item['link_path'];
    }
  }
  menu_rebuild();
}

function profiler_install_blocks($blocks, $config) {
  install_include(array('block'));

  // Rehash the blocks. We store the global theme key so we can rehash the 
  // blocks with $key as the theme key.
  $theme = isset($config['theme']) ? $config['theme'] : '';
  global $theme_key;
  $orig_key = $theme_key;
  $theme_key = $theme;
  _block_rehash();
  $theme_key = $orig_key;

  foreach($blocks as $block) {
    if (isset($block['module'])) {
      $theme = isset($block['theme']) ? $block['theme'] : $theme;

      // Check to see if this is a custom block.
      if ($block['module'] == 'block') {
        if ($block['format']) {
          _profiler_format_set_fid($block['format']);
        }
        $format = $block['format'] ? $block['format'] : NULL;
        $block['bid'] = install_create_custom_block($block['body'], $block['description'], $format);
  
        // Try to enable the block.
        if ($theme && $block['region']) {
          $block['status'] = isset($block['status']) ? $block['status'] : 1;
          install_add_block('block', $block['bid'], $theme, $block['status'], $block['weight'], $block['region'], $block['visibility'], $block['pages'], $block['custom'], $block['throttle'], $block['title']);
        }
      }
      // Otherwise, just enable the block.
      else if ($theme && isset($block['region']) && isset($block['delta'])) {
        install_set_block($block['module'], $block['delta'], $theme, $block['region'], $block['weight'], $block['visibility'], $block['pages'], $block['custom'], $block['throttle'], $block['title']);
      }
    }
  }
}

function profiler_install_theme($key) {
  system_theme_data();
  variable_set('theme_default', $key);
  list_themes(TRUE);
  system_initialize_theme_blocks($key);
  db_query("UPDATE {system} SET status = 1 WHERE type = 'theme' and name = '%s'", $key);
  list_themes(TRUE);
  menu_rebuild();
  drupal_rebuild_theme_registry();
}

function _profiler_format_set_fid(&$format) {
  // Get the input format, if specified.
  $sql = "SELECT format FROM {filter_formats} WHERE name = '%s'";
  if (isset($format) && is_string($format) && ($format_id = db_result(db_query($sql, $format)))) {
    $format = $format_id;
  }
  // Clean up if no input format was found.
  else {
    unset($format);
  }

  return $format;
}