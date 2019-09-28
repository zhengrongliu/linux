/*
 * vhci.h
 *
 *  Created on: Nov 24, 2018
 *      Author: Zhengrong Liu<towering@126.com>
 */

#ifndef __VHCI_H__
#define __VHCI_H__

#if defined(__GNUC__)
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#elif defined(WINNT)
typedef INT8 int8_t;
typedef UINT8 uint8_t;
typedef INT16 int16_t;
typedef UINT16 uint16_t;
typedef INT32 int32_t;
typedef UINT32 uint32_t;
typedef INT64 int64_t;
typedef UINT64 uint64_t;

#define __packed /* */
#define container_of(ptr,type,member) CONTAINING_RECORD(ptr,type,member)

#pragma pack(push,1)
#endif


#define VHCI_CMD 		0x00
#define VHCI_STATUS		0x04
#define VHCI_FEATURE		0x08
#define VHCI_INTMASK		0x0c

#define VHCI_RING_BASE		0x10
#define VHCI_RING_SIZE		0x14
#define VHCI_CONFIG			0x18

#define VHCI_DONE		0x20
#define VHCI_PARAM		0x24

#define VHCI_PORT_BASE		0x30
#define VHCI_PORT_STATUS_	0x0
#define VHCI_PORT_CMD_		0x4
#define VHCI_PORT_SIZE		0x8
#define VHCI_PORT_STATUS(x)	(VHCI_PORT_BASE + VHCI_PORT_STATUS_ + (x)*VHCI_PORT_SIZE)
#define VHCI_PORT_CMD(x)	(VHCI_PORT_BASE + VHCI_PORT_CMD_ + (x)*VHCI_PORT_SIZE)
#define VHCI_PORT_LIMIT(nr)	(VHCI_PORT_CMD(nr))

#define VHCI_CMD_INIT		0x01
#define VHCI_CMD_START		0x02
#define VHCI_CMD_STOP		0x03
#define VHCI_CMD_RESET		0x04
#define VHCI_CMD_SUBMIT		0x05
#define VHCI_CMD_CANCEL		0x06

#define VHCI_CONFIG_BUS_MODE (0<<0)
#define VHCI_CONFIG_SYS_MODE (1<<0)

#define VHCI_PORT_CMD_RESET	0x01

#define VHCI_ST_READY		(0<<0)
#define VHCI_ST_BUSY		(1<<0)
#define VHCI_ST_PORT_CHANGED	(1<<1)
#define VHCI_ST_XFER_DONE	(1<<2)


#define VHCI_FT_USB2PORTNR_OFFSET	0
#define VHCI_FT_USB2PORTNR_MASK	(0x3f)
#define VHCI_FT_USB2PORTNR(x)	(((x)&VHCI_FT_USB2PORTNR_MASK)<<VHCI_FT_USB2PORTNR_OFFSET)
#define VHCI_FT_USB3PORTNR_OFFSET	6
#define VHCI_FT_USB3PORTNR_MASK	(0x3f)
#define VHCI_FT_USB3PORTNR(x)	(((x)&VHCI_FT_USB2PORTNR_MASK)<<VHCI_FT_USB2PORTNR_OFFSET)


#define VHCI_PORT_ST_C_CONNECTION	(1<<0)
#define VHCI_PORT_ST_C_ENABLE		(1<<1)
#define VHCI_PORT_ST_C_RESET		(1<<2)
#define VHCI_PORT_ST_C_MASK			(0xff)
#define VHCI_PORT_ST_CONNECTION		(1<<16)
#define VHCI_PORT_ST_ENABLE		(1<<17)
#define VHCI_PORT_ST_RESET		(1<<18)
#define VHCI_PORT_ST_POWER		(1<<19)
#define VHCI_PORT_ST_SPEED_LOW		(0<<28)
#define VHCI_PORT_ST_SPEED_FULL		(1<<28)
#define VHCI_PORT_ST_SPEED_HIGH		(2<<28)
#define VHCI_PORT_ST_SPEED_SUPER	(3<<28)
#define VHCI_PORT_ST_SPEED_MASK		(0xf<<28)


#define VHCI_NODE_TYPE_HEAD	0x01
#define VHCI_NODE_TYPE_SEGMENT	0x02


#define VHCI_REQ_TYPE_ISOCHRONOUS		0
#define VHCI_REQ_TYPE_INTERRUPT			1
#define VHCI_REQ_TYPE_CONTROL			2
#define VHCI_REQ_TYPE_BULK			3
#define VHCI_REQ_TYPE_MASK			3
#define VHCI_REQ_SHORT_NOT_OK	(1<<2) /*short not ok */
#define VHCI_REQ_ZERO_PACKET	(1<<3) /* zero packet */
#define VHCI_REQ_ISO_ASAP		(1<<4) /* iso asap */
#define VHCI_REQ_DIR_IN			(1<<7) /* dir in */

#define VHCI_NODE_STATUS_USED	(1<<0)
#define VHCI_NODE_STATUS_LAST	(1<<1)

#define VHCI_HEAD_STATUS_XFER_OK	(0<<8)
#define VHCI_HEAD_STATUS_XFER_CANCEL	(1<<8)
#define VHCI_HEAD_STATUS_ERROR_NODEV	(2<<8)
#define VHCI_HEAD_STATUS_ERROR_IOERR	(3<<8)
#define VHCI_HEAD_STATUS_XFER_STALL	(4<<8)
#define VHCI_HEAD_STATUS_ERROR_BABBLE	(5<<8)
#define VHCI_HEAD_STATUS_ERROR_IO	(6<<8)
#define VHCI_HEAD_STATUS_XFER_MASK	(0xff<<8)

#define VHCI_SEGMENT_STATUS_XFER_OK	(0<<8)
#define VHCI_SEGMENT_STATUS_XFER_MASK	(0xff<<8)



#define VHCI_RING_INVALID_NODE ((uint16_t)~0)

struct vhci_ringnode_list{
	uint16_t prev;
	uint16_t next;
};

struct vhci_ringnode_base{
	uint8_t type;
	uint8_t request;
	uint16_t status;

	struct vhci_ringnode_list list;

	uint32_t reserve[4];
}__packed;


struct vhci_ringnode_head {
	uint8_t type;
	uint8_t request;
	uint16_t status;

	struct vhci_ringnode_list list;

	uint8_t epnum;
	uint8_t addr;
	uint16_t stream;

	uint16_t segments;
	uint16_t reserve[1];
	uint64_t length;
}__packed;

struct vhci_ringnode_segment{
	uint8_t type;
	uint8_t request;
	uint16_t status;

	struct vhci_ringnode_list list;

	uint64_t addr;
	uint64_t length;
}__packed;



union vhci_ringnode {
	struct vhci_ringnode_base base;
	struct vhci_ringnode_head head;
	struct vhci_ringnode_segment segment;
};


#if defined(WINNT)
#pragma pack(pop)
#endif


#define vhci_node_list_entry(ptr,type,element) container_of(ptr,type,element)

/*
 * vhci_node_list_next - get next node list of current
 *
 * @base - the base address of node list
 * @list - the current node list
 */
static inline struct vhci_ringnode_list * vhci_node_list_next(struct vhci_ringnode_base *base,
		struct vhci_ringnode_list *list)
{
	return &base[list->next].list;
}

/*
 * vhci_node_list_prev - get prev node list of current
 *
 * @base - the base address of node list
 * @list - the current node list
 */
static inline struct vhci_ringnode_list * vhci_node_list_prev(struct vhci_ringnode_base *base,
		struct vhci_ringnode_list *list)
{
	return &base[list->prev].list;
}

/*
 * vhci_node_list_index - get index of node list
 *
 * @base - the base address of node list
 * @list - the node list
 */
static inline uint16_t vhci_node_list_index(struct vhci_ringnode_base *base ,
		struct vhci_ringnode_list *list)
{

	return (uint16_t)(vhci_node_list_entry(list,struct vhci_ringnode_base,list) - base);
}

/*
 * vhci_node_list_init - init the node list
 * set #prev and #next to self
 *
 * @base - the base address of node list
 * @list - the node list
 */

static inline void vhci_node_list_init(struct vhci_ringnode_base *base,
		struct vhci_ringnode_list *list)
{
	uint16_t index ;

	index = vhci_node_list_index(base,list);

	list->prev = index;
	list->next = index;
}

/*
 * vhci_node_list_insert_tail - insert node to tail of head
 *
 * @base - the base address of node list
 * @head - the head of list
 * @new - the new node
 */
static inline void vhci_node_list_insert_tail(struct vhci_ringnode_base *base,
		struct vhci_ringnode_list *head ,struct vhci_ringnode_list *new)
{
	uint16_t new_index ;
	struct vhci_ringnode_list *tail;

	tail = vhci_node_list_prev(base,head);

	new_index = vhci_node_list_index(base,new);

	new->prev = head->prev;
	new->next = tail->next;

	head->prev = new_index;
	tail->next = new_index;

}

/*
 * vhci_node_list_remove - remove node of list
 *
 * @base - the base address of node list
 * @list - the node of list
 */
static inline void vhci_node_list_remove(struct vhci_ringnode_base *base,
		struct vhci_ringnode_list *list)
{
	struct vhci_ringnode_list *prev;
	struct vhci_ringnode_list *next;

	prev = vhci_node_list_prev(base,list);
	next = vhci_node_list_next(base,list);

	prev->next = list->next;
	next->prev = list->prev;

}

/*
 * vhci_node_list_next_segment - get next node list and turn it to segment
 */
static inline struct vhci_ringnode_segment *vhci_node_list_next_segment(
			struct vhci_ringnode_base *base,
			struct vhci_ringnode_list *list)
{
	return vhci_node_list_entry(vhci_node_list_next(base,list),
			struct vhci_ringnode_segment,list);
}


/*
 * vhci_node_list_prev_segment - get prev node list and turn it to segment
 */
static inline struct vhci_ringnode_segment *vhci_node_list_prev_segment(
			struct vhci_ringnode_base *base,
			struct vhci_ringnode_list *list)
{
	return vhci_node_list_entry(vhci_node_list_prev(base,list),
			struct vhci_ringnode_segment,list);
}




#endif
