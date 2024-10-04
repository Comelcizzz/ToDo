// Data Transfer Object (DTO) for User
// This class is used to define the structure of the user data 
// that will be transferred between the application layers (e.g., 
// from the database to the client). It ensures that only the 
// relevant data is exposed and helps maintain a consistent 
// format.

module.exports = class UserDto {
    // Unique identifier for the user
    id;
    // Email address of the user
    email;
    // List of projects associated with the user
    projects;

    // Constructor to initialize the UserDto with data from the model
    constructor(model) {
        // Set the ID, ensuring it's taken from the model's '_id' property
        this.id = model._id; 
        // Set the email from the model
        this.email = model.email;
        // Set the projects from the model
        this.projects = model.projects;
    }
}
