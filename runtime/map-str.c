/* -*- linux-c -*- */

/** @file map-str.c
 * @brief Map functions to set and get strings
 */

/* from map.c */
void str_copy(char *dest, char *src);

void str_add(void *dest, char *val)
{
	char *dst = (char *)dest;
	int len = strlen(val);
	int len1 = strlen(dst);
	int num = MAP_STRING_LENGTH - 1 - len1;

	if (len > num)
		len = num;
	strncpy (&dst[len1], val, len);
	dst[len + len1] = 0;
}

void __stp_map_set_str (MAP map, char *val, int add)
{
	struct map_node *m;

	if (map == NULL)
		return;

	if (map->create) {
		if (val == 0 && !map->no_wrap)
			return;

		m = __stp_map_create (map);
		if (!m)
			return;
		
		/* set the value */
		dbug ("m=%lx offset=%lx\n", (long)m, (long)map->data_offset);
		str_copy((void *)((long)m + map->data_offset), val);
	} else {
		if (map->key == NULL)
			return;
		
		if (val) {
			if (add)
				str_add((void *)((long)map->key + map->data_offset), val);
			else
				str_copy((void *)((long)map->key + map->data_offset), val);
		} else if (!add) {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}

/** Set the current element's value to a string.
 * This sets the current element's value to a string. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing new value.
 * @sa _stp_map_set()
 * @ingroup map_set
 */
#define _stp_map_set_str(map,val) __stp_map_set_str(map,val,0)
/** Add to the current element's string value.
 * This sets the current element's value to a string consisting of the old
 * contents followed by the new string. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing value to append.
 * @ingroup map_set
 */
#define _stp_map_add_str(map,val) __stp_map_set_str(map,val,1)

/** Get the current element's string value.
 * This gets the current element's string value. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If no current element (key) is set for the map, this function 
 * returns NULL.
 * @param map
 * @sa _stp_map_set()
 * @ingroup map_set
 */
char *_stp_map_get_str (MAP map)
{
	struct map_node *m;
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	dbug ("key %lx\n", (long)map->key);
	m = (struct map_node *)map->key;
	return (char *)((long)m + map->data_offset);
}

/** Set the current element's value to String.
 * This sets the current element's value to a String. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing new value.
 * @sa _stp_map_set()
 * @ingroup map_set
 */

void _stp_map_set_string (MAP map, String str)
{
	__stp_map_set_str (map, str->buf, 0);
}

