import sys
import os
try:
    from PIL import Image
except ImportError:
    print("The Python Imaging Library (PIL) is not installed.")
    print("Please install it using one of the following methods:")
    print(" pip install pillow #(recommended)")
    print(" pip install PIL")
    print(" or follow the installation instructions for your platform at https://pillow.readthedocs.io")
    sys.exit(1)

def convert_image_to_bitmap(image_path, output_file=None, converted_image_path=None, target_width=536, target_height=240):
    # Set default output file if not provided
    if output_file is None:
        output_file = os.path.splitext(image_path)[0] + ".py"

    with Image.open(image_path) as img:
        # Resize the image to fit within the target dimensions while maintaining aspect ratio
        img.thumbnail((target_width, target_height), Image.Resampling.LANCZOS)
        img = img.convert("RGB")

        if converted_image_path is not None:
            img.save(converted_image_path, "PNG")
            print(f"Converted image saved as: {converted_image_path}")

        width, height = img.size

        # Create a bytearray to store the bitmap data and iterate over each pixel in the image to convert to 565 format
        bitmap_data = bytearray(width * height * 2)
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                color565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                index = (y * width + x) * 2
                bitmap_data[index] = (color565 >> 8) & 0xFF
                bitmap_data[index + 1] = color565 & 0xFF

    # Write the bitmap data to the output file in Python bytearray format
    with open(output_file, "w") as f:
        f.write("__bitmap = \\\n")
        for i in range(0, len(bitmap_data), 16):
            chunk = bitmap_data[i:i+16]
            hex_data = "".join(f"\\x{c:02X}" for c in chunk)
            f.write(f"b'{hex_data}' \\\n")
        f.write("\n")
        f.write(f"WIDTH = const({width})\n")
        f.write(f"HEIGHT = const({height})\n")
        f.write("BITMAP = memoryview(__bitmap)\n")

    print(f"Bitmap data saved as: {output_file}")

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Convert an image to a micropython compatable bytearray bitmap for a RM67162 with 536x240 screen. \n Note that the image will be resized to fit within the target dimensions while maintaining original aspect ratio. \n all arguments are except for image_path.")
    parser.add_argument("image_path", help="Path to the input image.")
    parser.add_argument("output_file", nargs="?", help="Path to the output .py file. Defaults to the same path as input image with .py extension.")
    parser.add_argument("-w", "--width", type=int, default=536, help="Target width for the image. Default is 536.")
    parser.add_argument("-ht", "--height", type=int, default=240, help="Target height for the image. Default is 240.")
    parser.add_argument("-d", "-debug", "--converted_image_path", nargs="?", const="", help="Path to save the resized image for debugging purposes. If no path is provided, the converted image will be saved as <input>_conv.png.")

    args = parser.parse_args()

    # Check if the input file is a valid image file
    if not args.image_path.lower().endswith(('.png', '.jpg', '.jpeg')):
        print("Error: The input file must be a .png or .jpg/.jpeg image.")
        sys.exit(1)

    if args.converted_image_path == "":
        args.converted_image_path = os.path.splitext(args.image_path)[0] + "_conv.png"
    elif args.converted_image_path is None:
        args.converted_image_path = None

    convert_image_to_bitmap(args.image_path, args.output_file, converted_image_path=args.converted_image_path, target_width=args.width, target_height=args.height)