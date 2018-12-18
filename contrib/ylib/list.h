/* Copyright (c) 2012-2015 Yoran Heling

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef YLIB_LIST_H
#define YLIB_LIST_H

/* All macros assume that the next/prev pointers are embedded in the structure
 * of each node, and that they are named 'next' and 'prev'. All macros take a
 * `list' argument, which is a struct that contains a `head' and `tail'
 * pointer.
 *
 * (Not implemented yet) All macros starting with 's' (e.g. slist_ instead of
 * list_) operate on singly-linked lists. The nodes only need to have a 'next'
 * pointer.
 *
 * All macros containing the 'h' (hlist_, shlist_) flag take only a single
 * pointer variable as list argument rather than a struct. This pointer is
 * treated as the `head' of a list.
 *
 * All macros containing the 'p' flag (plist_, splist_) accept an additional
 * prefix argument. This argument is prefixed to the names of the 'next' and
 * 'prev' pointers.
 *
 * All pointers point directly to the outer-most structure of the node and not
 * to some specially-embedded structure. The latter is the approach taken, for
 * example, in the Linux kernel. (https://lwn.net/Articles/336255/)
 *
 * Circular lists are not supported. List pointers should be zero-initialized
 * (i.e. NULL,NULL) before use. These macros do not keep a count of the number
 * of items in the list.
 */


/* Insert an element in the list before _next.  */
#define plist_insert_before(_p, _l, _n, _next) do {\
		(_n)->_p##next = (_next);\
		(_n)->_p##prev = (_next) ? (_next)->_p##prev : NULL;\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n);\
		else               (_l).tail = (_n);\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n);\
		else               (_l).head = (_n);\
	} while(0)

#define list_insert_before(_l, _n, _next) plist_insert_before(, _l, _n, _next)


#define hplist_insert_before(_p, _l, _n, _next) do {\
		(_n)->_p##next = (_next);\
		(_n)->_p##prev = (_next) ? (_next)->_p##prev : NULL;\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n);\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n);\
		else               (_l) = (_n);\
	} while(0)

#define hlist_insert_before(_l, _n, _next) hplist_insert_before(, _l, _n, _next)


/* Insert an item at the head of the list */
#define plist_prepend(_p, _l, _n)  plist_insert_before(_p, _l, _n, (_l).head)
#define list_prepend(_l, _n)       list_insert_before(_l, _n, (_l).head)
#define hplist_prepend(_p, _l, _n) hplist_insert_before(_p, _l, _n, _l)
#define hlist_prepend(_l, _n)      hlist_insert_before(_l, _n, _l)


/* Insert an element in the list after _prev. */
#define plist_insert_after(_p, _l, _n, _prev) do {\
		(_n)->_p##prev = (_prev);\
		(_n)->_p##next = (_prev) ? (_prev)->_p##next : NULL;\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n);\
		else               (_l).tail = (_n);\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n);\
		else               (_l).head = (_n);\
	} while(0)

#define list_insert_after(_l, _n, _prev) plist_insert_after(, _l, _n, _prev)


#define hplist_insert_after(_p, _l, _n, _prev) do {\
		(_n)->_p##prev = (_prev);\
		(_n)->_p##next = (_prev) ? (_prev)->_p##next : NULL;\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n);\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n);\
		else               (_l) = (_n);\
	} while(0)

#define hlist_insert_after(_l, _n, _prev) hplist_insert_after(, _l, _n, _prev)


/* Insert an item at the end of the list */
#define plist_append(_p, _l, _n)  plist_insert_after(_p, _l, _n, (_l).tail)
#define list_append(_l, _n)       list_insert_after(_l, _n, (_l).tail)


/* Remove an element from the list.
 * To remove the first element:
 *   list_remove(l, l.head);
 *   hlist_remove(l, l);
 * To remove the last element:
 *   list_remove(l, l.tail);
 */
#define plist_remove(_p, _l, _n) do {\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n)->_p##prev;\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n)->_p##next;\
		if((_n) == (_l)._p##head) (_l)._p##head = (_n)->_p##next;\
		if((_n) == (_l)._p##tail) (_l)._p##tail = (_n)->_p##prev;\
	} while(0)

#define list_remove(_l, _n) plist_remove(, _l, _n)


#define hplist_remove(_p, _l, _n) do {\
		if((_n)->_p##next) (_n)->_p##next->_p##prev = (_n)->_p##prev;\
		if((_n)->_p##prev) (_n)->_p##prev->_p##next = (_n)->_p##next;\
		if((_n) == (_l)) (_l) = (_n)->_p##next;\
	} while(0)

#define hlist_remove(_l, _n) hplist_remove(, _l, _n)


#endif
/* vim: set noet sw=4 ts=4: */
