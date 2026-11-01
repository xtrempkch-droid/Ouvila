/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OUVILA_INPUT_POLLDEV_COMPAT_H
#define _OUVILA_INPUT_POLLDEV_COMPAT_H

#include <linux/input.h>
#include <linux/slab.h>

struct input_polled_dev {
	struct input_dev *input;
	void *private;
	void (*poll)(struct input_polled_dev *poll_dev);
	unsigned int poll_interval;
	void (*open)(struct input_polled_dev *poll_dev);
	void (*close)(struct input_polled_dev *poll_dev);
};

static inline void ouvila_input_polled_callback(struct input_dev *input)
{
	struct input_polled_dev *poll_dev = input_get_drvdata(input);

	poll_dev->poll(poll_dev);
}

static inline int ouvila_input_polled_open(struct input_dev *input)
{
	struct input_polled_dev *poll_dev = input_get_drvdata(input);

	if (poll_dev->open)
		poll_dev->open(poll_dev);

	return 0;
}

static inline void ouvila_input_polled_close(struct input_dev *input)
{
	struct input_polled_dev *poll_dev = input_get_drvdata(input);

	if (poll_dev->close)
		poll_dev->close(poll_dev);
}

static inline struct input_polled_dev *
devm_input_allocate_polled_device(struct device *dev)
{
	struct input_polled_dev *poll_dev;

	poll_dev = devm_kzalloc(dev, sizeof(*poll_dev), GFP_KERNEL);
	if (!poll_dev)
		return NULL;

	poll_dev->input = devm_input_allocate_device(dev);
	if (!poll_dev->input)
		return NULL;

	return poll_dev;
}

static inline int input_register_polled_device(struct input_polled_dev *poll_dev)
{
	struct input_dev *input = poll_dev->input;
	int error;

	error = input_setup_polling(input, ouvila_input_polled_callback);
	if (error)
		return error;

	input_set_poll_interval(input, poll_dev->poll_interval);
	input->open = ouvila_input_polled_open;
	input->close = ouvila_input_polled_close;
	input_set_drvdata(input, poll_dev);

	return input_register_device(input);
}

#endif
