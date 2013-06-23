<?php
/**
 * Get path to files
 *
 * @return bool|string
 *   Returns bool false or string path to files
 */
function image_compressor_get_files_path() {
  if(!$wrapper = file_stream_wrapper_get_instance_by_uri('public://')) {
    drupal_set_message("Could not get file stream wrapper for public://. Make sure your <a href='/admin/config/media/file-system'>public path configuration</a> is correct.", 'error');
    return false;
  }
  if(!$path = $wrapper->realpath()) {
    drupal_set_message("Could not get public:// path. Make sure your <a href='/admin/config/media/file-system'>public path configuration</a> is correct.", 'error');
    return false;
  }
  return $path;
}

/**
 * Get path to backup files
 *
 * @return bool|string
 *   Returns bool false or backup folder path
 */
function image_compressor_get_backup_path() {
  if(!$path = image_compressor_get_files_path()) {
    return false;
  }
  $backuppath = $path . '/_compressor_backup';
  try {
    if(!is_writeable($backuppath)) {
      if(!is_dir($backuppath)) {
        // Create folder
        mkdir($backuppath, 0755, true);
      }
      // Backup folder must exist
      if(!is_dir($backuppath)) {
        form_set_error("", "Could not create directory: {$backuppath}. Make sure permissions are correct.");
        return false;
      }
    }
    if(!is_writeable($backuppath)) {
      drupal_set_message("Backup folder is not writeable! Make sure the following path exists and that the permissions are properly set: ".$backuppath, 'error');
      return false;
    }
  }
  catch(Exception $e) {
    drupal_set_message("Backup folder could not be created: ".$e->getMessage().". Make sure the following path exists and that the permissions are properly set: ".$backuppath, 'error');
    return false;
  }
  return $backuppath;
}

/**
 * Get total space saved by compressor (as long as backup files are kept)
 *
 * @return int
 */
function image_compressor_get_total_space_saved() {

  // Get paths
  if(!$path = image_compressor_get_files_path()) return false;
  if(!$backuppath = image_compressor_get_backup_path()) return false;

  // Calculate compressed vs non-compressed size
  $size = 0;
  if($handle = opendir($backuppath)) {
    while (false !== ($entry = readdir($handle))) {
      if(substr($entry,0,1) != '.') {
        if(is_file($path.'/'.$entry)) {
          $size += ( filesize($backuppath.'/'.$entry) - filesize($path.'/'.$entry) );
        }
      }
    }
    closedir($handle);
  }

  // Return amount of space saved by compression
  return $size;
}

/**
 * Batch compress images
 *
 * @param $filename
 *   String filename (in media path) to process
 * @param $index
 *   Index counter
 * @param $total
 *   Total number of files in media path
 * @param object $context
 *   For batch communication
 * @return bool
 */
function image_compressor_batch($filename, $index, $total, &$context) {

  // Get config
  $maxwidth = (int) variable_get('image_compressor_width', 10000);
  $maxheight = (int) variable_get('image_compressor_height', 10000);
  $backup = (int) variable_get('image_compressor_backup', 1);

  // Init on first execution
  if(!$index) {
    // Temporarily set jpeg quality for compressor
    global $conf;
    $conf['image_jpeg_quality'] = variable_get('image_compressor_jpegquality', $conf['image_jpeg_quality']);
    // Update messages
    if($context) {
      if(empty($context['sandbox'])) {
        $context['sandbox']['progress'] = 0;
        $context['sandbox']['max'] = $total;
        $_SESSION['timer_start'] = time();
        $_SESSION['restored_images'] = 0;
      }
    }
  }

  // Get paths
  if(!$path = image_compressor_get_files_path()) return false;
  if(!$backuppath = image_compressor_get_backup_path()) return false;

  // Get source/target file
  $src = $target = $path.'/'.$filename;
  $src_is_backup = false;
  // Get image dimensions (and verify that the file is an image at the same time)
  $imagesize = getimagesize($src);
  // Only process images
  if($imagesize) {
    // Backup files?
    if($backup) {
      // Only backup images larger than max w/h
      if($maxwidth < $imagesize[0] || $maxheight < $imagesize[1]) {
        // Use backup image as source
        $backupsrc = $backuppath.'/'.$filename;
        if(!is_file($backupsrc)) {
          copy($src, $backupsrc);
        }
        else {
          // Backup file already exists, read imagesize from backup file
          $imagesize = getimagesize($backupsrc);
        }
        if(!is_file($backupsrc)) {
          $context['message'] = "Failed to backup {$src} [skipping file]";
          drupal_set_message("Failed to backup {$src} [skipping file]", 'error');
          $backupsrc = null;
        }
        $src = $backupsrc;
        $src_is_backup = true;
      }
    }
    // Only process images larger than max w/h
    if($maxwidth >= $imagesize[0] && $maxheight >= $imagesize[1]) {
      // Image is smaller than max w/h
      if($src_is_backup) {
        // Restore image from backup
        try {
          unlink($path.'/'.$filename);
          rename($backuppath.'/'.$filename, $path.'/'.$filename);
          $context['message'] = ' Restoring from backup: '.$filename;
          $_SESSION['restored_images']++;
        }
        catch(Exception $e) {
          $context['message'] = 'Failed to restore backup of '.$filename.': '.$e->getMessage();
          drupal_set_message('Failed to restore backup of '.$filename.': '.$e->getMessage(), 'error');
        }
      }
      else {
        // Not in backup, skip image
        $context['message'] = 'Scanning...';
      }
    }
    else {
      // Image is larger than max w/h
      // Maintain aspect ratio
      $size = array('width' => $imagesize[0], 'height' => $imagesize[1]);
      image_dimensions_scale($size, $maxwidth, $maxheight);
      // Resize image
      if($context) {
        $context['message'] = 'Compressing: '.check_plain($filename).'<br/>Original size: '.$imagesize[0].'x'.$imagesize[1].' px, '.format_size(filesize($src)).'<br/>Target size: '.$size['width'].'x'.$size['height'].' px';
      }
      try {
        $img = image_load($src);
        image_resize($img, $size['width'], $size['height']);
        image_save($img, $target);
        if($context) {
          $context['results'][] = $filename;
        }
      }
      catch(Exception $e) {
        if($context) {
          $context['message'] = 'Failed to compress '.check_plain($filename).': '.$e->getMessage();
          drupal_set_message('Failed to compress '.check_plain($filename).': '.$e->getMessage(), 'error');
        }
      }
    }
  }

  if($context) {
    $context['sandbox']['progress']++;
  }

  if(($index+1)==$total) {
    if($_SESSION['restored_images']) {
      drupal_set_message("Restored {$_SESSION['restored_images']} images from backup", 'status');
    }
    if($context) {
      $context['message'] = 'Image compressor finished';
      $context['finished'] = 1;
    }
  }

  return true;
}

function image_compressor_batch_finished($success, $results, $operations) {
  // Remove "configuration saved" message
  array_pop($_SESSION['messages']['status']);
  // The 'success' parameter means no fatal PHP errors were detected. All
  // other error management should be handled using 'results'.
  if (!$success) {
    drupal_set_message('Finished with an error.', 'error');
  }
  if(!count($results)) {
    drupal_set_message('Compressed '.(count($results)).' images', 'status');
    return;
  }
  // Calculate time
  $seconds_passed = time() - $_SESSION['timer_start'];
  if($hours = floor($seconds_passed / 3600)) $seconds_passed -= ($hours * 3600);
  if($minutes = floor($seconds_passed / 60)) $seconds_passed -= ($minutes * 60);
  $time_str = $hours ? ($hours<10?'0':'') . $hours : '00';
  $time_str .= ':';
  $time_str .= $minutes ? ($minutes<10?'0':'') . $minutes : '00';
  $time_str .= ':';
  $time_str .= ($seconds_passed<10?'0':'') . $seconds_passed;
  drupal_set_message('Compressed '.(count($results)).' images in '.$time_str, 'status');
}