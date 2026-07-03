## buffer: gst_buffer_add_custom_meta_with_structure function

New `gst_buffer_add_custom_meta_with_structure(GstBuffer * buffer, const gchar * name,
GstStructure * structure)` API to Set structure during CustomMeta creation
This bypasses the python API limitation where getting Structure from a CustomMeta
would return a copy, thus making impossible to set data to the Structure.
